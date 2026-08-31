package io.onetwogoyeah.one_two_go_yeah

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private lateinit var playback: PcmPlaybackOutput

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        playback = PcmPlaybackOutput(this)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "io.onetwogoyeah/playback",
        ).setMethodCallHandler(playback::handle)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "io.onetwogoyeah/files",
        ).setMethodCallHandler { call, result ->
            if (call.method != "linkNoReplace") {
                result.notImplemented()
                return@setMethodCallHandler
            }
            val source = call.argument<String>("sourcePath")
            val target = call.argument<String>("targetPath")
            if (source == null || target == null) {
                result.error("invalid-arguments", "File paths are required.", null)
                return@setMethodCallHandler
            }
            try {
                Os.link(source, target)
                result.success(true)
            } catch (error: ErrnoException) {
                if (error.errno == OsConstants.EEXIST) {
                    result.success(false)
                } else {
                    result.error("file-publish-error", error.message, error.errno)
                }
            }
        }
    }

    override fun onDestroy() {
        if (::playback.isInitialized) {
            playback.dispose()
        }
        super.onDestroy()
    }
}

private class PcmPlaybackOutput(context: Context) {
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var track: AudioTrack? = null
    private var totalFrames = 0L
    private var paused = true

    private val focusListener = AudioManager.OnAudioFocusChangeListener { change ->
        if (change == AudioManager.AUDIOFOCUS_LOSS ||
            change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT ||
            change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
            track?.pause()
            paused = true
        }
    }

    fun handle(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "start" -> {
                    val sampleRate = call.argument<Int>("sampleRate")
                        ?: error("sampleRate is required")
                    val pcm = call.argument<ByteArray>("pcm16le")
                        ?: error("pcm16le is required")
                    start(sampleRate, pcm)
                    result.success(null)
                }
                "query" -> result.success(state())
                "pause" -> {
                    track?.pause()
                    paused = true
                    result.success(state())
                }
                "dispose" -> {
                    dispose()
                    result.success(null)
                }
                else -> result.notImplemented()
            }
        } catch (error: Throwable) {
            result.error("playback-error", error.message, null)
        }
    }

    private fun start(sampleRate: Int, pcm: ByteArray) {
        require(sampleRate > 0) { "sampleRate must be positive" }
        require(pcm.size % 4 == 0) { "PCM must contain complete stereo frames" }
        disposeTrack()
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(sampleRate)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
            .build()
        val next = AudioTrack.Builder()
            .setAudioAttributes(attributes)
            .setAudioFormat(format)
            .setTransferMode(AudioTrack.MODE_STATIC)
            .setBufferSizeInBytes(pcm.size)
            .build()
        val written = next.write(pcm, 0, pcm.size)
        if (written != pcm.size) {
            next.release()
            error("Unable to write the complete playback buffer")
        }
        audioManager.requestAudioFocus(
            focusListener,
            AudioManager.STREAM_MUSIC,
            AudioManager.AUDIOFOCUS_GAIN,
        )
        totalFrames = pcm.size.toLong() / 4L
        paused = false
        track = next
        next.play()
    }

    private fun state(): Map<String, Any> {
        val current = track
        val frames = if (current == null) {
            0L
        } else {
            current.playbackHeadPosition.toLong() and 0xffffffffL
        }.coerceAtMost(totalFrames)
        val playing = current != null && !paused && frames < totalFrames &&
            current.playState == AudioTrack.PLAYSTATE_PLAYING
        return mapOf("playedFrames" to frames, "playing" to playing)
    }

    fun dispose() {
        disposeTrack()
        audioManager.abandonAudioFocus(focusListener)
    }

    private fun disposeTrack() {
        track?.run {
            stop()
            flush()
            release()
        }
        track = null
        totalFrames = 0L
        paused = true
    }
}
