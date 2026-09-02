package io.onetwogoyeah.one_two_go_yeah

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.provider.DocumentsContract
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.io.FileNotFoundException
import java.io.OutputStream

class MainActivity : FlutterActivity() {
    companion object {
        private const val CREATE_DOCUMENT_REQUEST = 0x4f54
    }

    private lateinit var playback: PcmPlaybackOutput
    private lateinit var documentWriter: DocumentWriter
    private var pendingCreateDocumentResult: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        playback = PcmPlaybackOutput(this)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "io.onetwogoyeah/playback",
        ).setMethodCallHandler(playback::handle)
        documentWriter = DocumentWriter(this)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            "io.onetwogoyeah/files",
        ).setMethodCallHandler { call, result ->
            try {
                when (call.method) {
                    "linkNoReplace" -> linkNoReplace(call, result)
                    "createDocument" -> createDocument(call, result)
                    else -> documentWriter.handle(call, result)
                }
            } catch (error: Throwable) {
                result.error(
                    "file-operation-error",
                    error.message,
                    mapOf(
                        "method" to call.method,
                        "exception" to error.javaClass.name,
                    ),
                )
            }
        }
    }

    private fun linkNoReplace(call: MethodCall, result: MethodChannel.Result) {
        val source = call.argument<String>("sourcePath")
        val target = call.argument<String>("targetPath")
        if (source == null || target == null) {
            result.error("invalid-arguments", "File paths are required.", null)
            return
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

    private fun createDocument(call: MethodCall, result: MethodChannel.Result) {
        if (pendingCreateDocumentResult != null) {
            result.error(
                "document-picker-active",
                "Another document picker is already active.",
                null,
            )
            return
        }
        val suggestedName = call.argument<String>("suggestedName")
        val mimeType = call.argument<String>("mimeType")
        if (suggestedName.isNullOrBlank() || mimeType.isNullOrBlank()) {
            result.error(
                "invalid-arguments",
                "A suggested name and MIME type are required.",
                null,
            )
            return
        }
        pendingCreateDocumentResult = result
        try {
            startActivityForResult(
                Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = mimeType
                    putExtra(Intent.EXTRA_TITLE, suggestedName)
                    addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                },
                CREATE_DOCUMENT_REQUEST,
            )
        } catch (error: Throwable) {
            pendingCreateDocumentResult = null
            throw error
        }
    }

    @Deprecated("Deprecated in Android")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode != CREATE_DOCUMENT_REQUEST) {
            super.onActivityResult(requestCode, resultCode, data)
            return
        }
        val pending = pendingCreateDocumentResult ?: return
        pendingCreateDocumentResult = null
        if (resultCode != Activity.RESULT_OK) {
            pending.success(null)
            return
        }
        val uri = data?.data
        if (uri == null) {
            pending.error(
                "document-picker-error",
                "The document picker returned no URI.",
                null,
            )
            return
        }
        pending.success(uri.toString())
    }

    override fun onDestroy() {
        if (::playback.isInitialized) {
            playback.dispose()
        }
        if (::documentWriter.isInitialized) {
            documentWriter.dispose()
        }
        pendingCreateDocumentResult = null
        super.onDestroy()
    }
}

private class DocumentWriter(private val context: Context) {
    private data class Session(val uri: Uri, val output: OutputStream)

    private val sessions = mutableMapOf<Int, Session>()
    private var nextSession = 1

    fun handle(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "openDocumentWrite" -> open(call, result)
            "writeDocumentChunk" -> write(call, result)
            "finishDocumentWrite" -> finish(call, result)
            "abortDocumentWrite" -> abort(call, result)
            "deleteDocument" -> delete(call, result)
            else -> result.notImplemented()
        }
    }

    private fun open(call: MethodCall, result: MethodChannel.Result) {
        val uri = requiredUri(call)
        // ACTION_CREATE_DOCUMENT always returns a new, empty document. "w" is
        // supported by more document providers than the optional "wt" mode.
        val output = context.contentResolver.openOutputStream(uri, "w")
            ?: throw FileNotFoundException("Unable to open document for writing: $uri")
        val id = nextSession++
        sessions[id] = Session(uri, output)
        result.success(id)
    }

    private fun write(call: MethodCall, result: MethodChannel.Result) {
        val session = requiredSession(call)
        val bytes = call.argument<ByteArray>("bytes")
            ?: error("Document bytes are required.")
        session.output.write(bytes)
        result.success(null)
    }

    private fun finish(call: MethodCall, result: MethodChannel.Result) {
        val id = requiredSessionId(call)
        val session = sessions.remove(id) ?: error("Unknown document write session: $id")
        try {
            session.output.flush()
        } finally {
            session.output.close()
        }
        result.success(null)
    }

    private fun abort(call: MethodCall, result: MethodChannel.Result) {
        val id = requiredSessionId(call)
        val uri = call.argument<String>("uri")?.let(Uri::parse)
            ?: sessions[id]?.uri
            ?: error("A document URI is required.")
        try {
            sessions.remove(id)?.output?.close()
        } finally {
            deleteUri(uri)
        }
        result.success(null)
    }

    private fun delete(call: MethodCall, result: MethodChannel.Result) {
        deleteUri(requiredUri(call))
        result.success(null)
    }

    private fun requiredSession(call: MethodCall): Session {
        val id = requiredSessionId(call)
        return sessions[id] ?: error("Unknown document write session: $id")
    }

    private fun requiredSessionId(call: MethodCall): Int =
        call.argument<Int>("session") ?: error("A document write session is required.")

    private fun requiredUri(call: MethodCall): Uri {
        val value = call.argument<String>("uri")
            ?: error("A document URI is required.")
        val uri = Uri.parse(value)
        require(value.isNotBlank() && uri.scheme == "content") {
            "A content document URI is required."
        }
        return uri
    }

    private fun deleteUri(uri: Uri) {
        try {
            DocumentsContract.deleteDocument(context.contentResolver, uri)
        } catch (_: FileNotFoundException) {
            // Deletion is idempotent so Flutter can safely repeat cleanup after
            // an aborted writer already removed the unfinished document.
        }
    }

    fun dispose() {
        val unfinished = sessions.values.toList()
        sessions.clear()
        for (session in unfinished) {
            try {
                session.output.close()
            } catch (_: Throwable) {
                // Continue cleanup even if a provider fails while closing.
            }
            try {
                deleteUri(session.uri)
            } catch (_: Throwable) {
                // The activity is already shutting down; cleanup was attempted.
            }
        }
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
