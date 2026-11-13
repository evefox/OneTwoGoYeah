package io.onetwogoyeah.one_two_go_yeah

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Build
import android.os.Process
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

object PlaybackEngineBridge {
    init {
        System.loadLibrary("otgy_native")
    }

    external fun renderNative(handle: Long, buffer: ByteArray, frameCount: Int)

    external fun isFinishedNative(handle: Long): Boolean
}

class SimulationPlaybackOutput(private val context: Context) {
    private val audioManager =
        context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var track: AudioTrack? = null
    private var worker: Thread? = null
    @Volatile private var running = false
    @Volatile private var paused = true
    @Volatile private var finished = false
    @Volatile private var latencyMs = 0
    @Volatile private var bufferFrames = 0
    @Volatile private var engineHandle = 0L
    private val lock = Object()

    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        if (change == AudioManager.AUDIOFOCUS_LOSS ||
            change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT ||
            change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK
        ) {
            pause()
        }
    }

    fun handle(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "start" -> {
                    val handle =
                        call.argument<Number>("engineHandle")?.toLong()
                            ?: error("engineHandle is required")
                    val rate = call.argument<Int>("sampleRate")
                        ?: error("sampleRate is required")
                    val frames = call.argument<Int>("blockFrames") ?: 256
                    start(handle, rate, frames)
                    result.success(null)
                }
                "pause" -> {
                    pause()
                    result.success(null)
                }
                "resume" -> {
                    resume()
                    result.success(null)
                }
                "preferredSampleRate" -> result.success(
                    audioManager.getProperty(
                        AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE
                    )?.toIntOrNull() ?: 0
                )
                "preferredFramesPerBuffer" -> result.success(
                    audioManager.getProperty(
                        AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER
                    )?.toIntOrNull() ?: 0
                )
                "query" -> result.success(state())
                "stop" -> {
                    stop()
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        } catch (error: Throwable) {
            result.error("simulation-playback-error", error.message, null)
        }
    }

    private fun start(handle: Long, rate: Int, frames: Int) {
        require(rate > 0) { "sampleRate must be positive" }
        require(frames > 0) { "blockFrames must be positive" }
        stop()
        engineHandle = handle
        val minBuffer = AudioTrack.getMinBufferSize(
            rate,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        require(minBuffer > 0) { "Unable to query the minimum audio buffer" }
        // One block beyond the platform minimum keeps the writer from
        // starving, while the minimum buffer keeps key-to-speaker latency
        // as small as the device allows.
        val bufferBytes = maxOf(minBuffer, frames * 4)
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(rate)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
            .build()
        val builder = AudioTrack.Builder()
            .setAudioAttributes(attributes)
            .setAudioFormat(format)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(bufferBytes)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
        }
        val next = builder.build()
        var actualBufferFrames = bufferBytes / 4
        // Shrink the effective queue to two device blocks when the track
        // supports it: one block holds the next write while the other is being
        // consumed, which keeps the key-to-speaker delay tiny without
        // underrunning on a normal Android scheduling jitter. The HAL adds
        // its own fixed latency on top.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                val shrunk = next.setBufferSizeInFrames(maxOf(frames, 64) * 2)
                if (shrunk > 0) {
                    actualBufferFrames = shrunk
                }
            } catch (_: Throwable) {
            }
        }
        bufferFrames = actualBufferFrames
        latencyMs = if (rate > 0) bufferFrames * 1000 / rate else 0
        audioManager.requestAudioFocus(
            focusListener,
            AudioManager.STREAM_MUSIC,
            AudioManager.AUDIOFOCUS_GAIN,
        )
        running = true
        paused = false
        finished = false
        track = next
        next.play()
        worker = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val buffer = ByteArray(frames * 4)
            while (running) {
                if (paused) {
                    synchronized(lock) {
                        if (running && paused) {
                            try {
                                lock.wait(20)
                            } catch (_: InterruptedException) {
                            }
                        }
                    }
                    continue
                }
                if (PlaybackEngineBridge.isFinishedNative(engineHandle)) {
                    finished = true
                    break
                }
                PlaybackEngineBridge.renderNative(engineHandle, buffer, frames)
                var offset = 0
                while (running && !paused && offset < buffer.size) {
                    val written = next.write(buffer, offset, buffer.size - offset)
                    if (written <= 0) {
                        break
                    }
                    offset += written
                }
            }
        }, "otgy-simulation-audio").also { it.start() }
    }

    private fun pause() {
        if (!paused) {
            paused = true
            try {
                track?.pause()
            } catch (_: Throwable) {
            }
            synchronized(lock) { lock.notifyAll() }
        }
    }

    private fun resume() {
        if (running && paused) {
            paused = false
            try {
                track?.play()
            } catch (_: Throwable) {
            }
            synchronized(lock) { lock.notifyAll() }
        }
    }

    private fun stop() {
        running = false
        paused = true
        synchronized(lock) { lock.notifyAll() }
        val current = track
        track = null
        try {
            current?.pause()
        } catch (_: Throwable) {
        }
        try {
            current?.flush()
        } catch (_: Throwable) {
        }
        try {
            current?.release()
        } catch (_: Throwable) {
        }
        worker?.join(500)
        worker = null
        engineHandle = 0L
        audioManager.abandonAudioFocus(focusListener)
    }

    private fun state(): Map<String, Any> {
        val current = track
        val frames = current?.playbackHeadPosition?.toLong() ?: 0L
        return mapOf(
            "playedFrames" to frames,
            "playing" to (running && !paused && !finished && current != null),
            "finished" to finished,
            "latencyMs" to latencyMs,
            "bufferFrames" to bufferFrames,
        )
    }

    fun dispose() {
        stop()
    }
}
