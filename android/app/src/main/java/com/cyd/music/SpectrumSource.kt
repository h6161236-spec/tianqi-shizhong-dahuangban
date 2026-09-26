package com.cyd.music

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.util.Log
import kotlin.concurrent.thread
import kotlin.math.max

/** Captures only the audio authorized by the app-selection dialog. */
class SpectrumSource(
    private val ctx: Context,
    private val onFrame: (IntArray) -> Unit
) {
    companion object {
        private const val TAG = "SpectrumSource"

        const val MODE_NONE = 0
        const val MODE_PROJECTION = 1

        private const val SAMPLE_RATE = 44100
        /** 和 Spectrum 的 fftSize 对齐:一帧刚好喂满一次 FFT */
        private const val FRAME_SAMPLES = 2048
        /** 频段数,要和板子的 SPEC_BANDS 一致 */
        private const val BANDS = 16
        /** 发送间隔:约 20fps,再快蓝牙吃不下也没必要 */
        private const val FRAME_INTERVAL_MS = 50L

        fun modeName(m: Int) = when (m) {
            MODE_PROJECTION -> "系统音频"
            else -> "未启动"
        }
    }

    private val spectrum = Spectrum(SAMPLE_RATE, FRAME_SAMPLES, BANDS)
    private val out = IntArray(BANDS)

    @Volatile private var running = false
    @Volatile private var worker: Thread? = null
    @Volatile private var record: AudioRecord? = null

    /** 当前音频采集状态,给界面显示 */
    @Volatile var mode: Int = MODE_NONE
        private set

    /** 最近一帧是不是静音(服务用它决定要不要停发 SPEC) */
    @Volatile var silent: Boolean = true
        private set

    fun start(projection: MediaProjection) {
        stop()
        spectrum.reset()
        val rec = openProjection(projection)
        if (rec == null) {
            MusicBus.status("音乐 App 不允许音频采集，歌词仍会同步")
            onFrame(IntArray(BANDS))
            return
        }
        running = true
        record = rec
        startCapture(rec)
    }

    fun stop() {
        running = false
        worker?.interrupt()
        worker = null
        try {
            record?.stop()
        } catch (_: Exception) {
        }
        try {
            record?.release()
        } catch (_: Exception) {
        }
        record = null
        mode = MODE_NONE
        silent = true
    }

    // ==================== App 音频 ====================

    @SuppressLint("MissingPermission")
    private fun openProjection(projection: MediaProjection?): AudioRecord? {
        if (projection == null) return null
        return try {
            val cfg = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                // 有些播放器把音乐标成 UNKNOWN,一起收进来
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()
            val rec = AudioRecord.Builder()
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setSampleRate(SAMPLE_RATE)
                        .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                        .build()
                )
                .setBufferSizeInBytes(bufferBytes())
                .setAudioPlaybackCaptureConfig(cfg)
                .build()
            if (rec.state != AudioRecord.STATE_INITIALIZED) {
                rec.release()
                return null
            }
            mode = MODE_PROJECTION
            rec
        } catch (e: Exception) {
            // 播放器给音频打了 FLAG_NO_MEDIA_PROJECTION 时会走到这
            Log.w(TAG, "AudioPlaybackCapture 打不开", e)
            null
        }
    }

    // ==================== 采集循环 ====================

    private fun startCapture(rec: AudioRecord) {
        MusicBus.status("频谱来源:${modeName(mode)}")
        worker = thread(name = "spec-capture", isDaemon = true) {
            val pcm = ShortArray(FRAME_SAMPLES)
            try {
                rec.startRecording()
            } catch (e: Exception) {
                Log.w(TAG, "startRecording 失败", e)
                MusicBus.status("录音启动失败: ${e.message}")
                return@thread
            }
            var lastSend = 0L
            try {
                while (running && !Thread.currentThread().isInterrupted) {
                    val n = try {
                        rec.read(pcm, 0, FRAME_SAMPLES)
                    } catch (e: Exception) {
                        Log.w(TAG, "read 失败", e); break
                    }
                    if (n <= 0) {
                        // ERROR_INVALID_OPERATION 之类,歇一下再试,别空转烧 CPU
                        Thread.sleep(20); continue
                    }

                    silent = spectrum.rms(pcm, n) < Spectrum.SILENCE_RMS

                    // 静音时不发:板子那边 MUSIC_IDLE_MS 到了才能回到时钟界面。
                    // 旧代码无条件发 SPEC,导致板子永远不切回时钟。
                    if (silent) {
                        spectrum.reset()
                        continue
                    }

                    val now = System.currentTimeMillis()
                    if (now - lastSend < FRAME_INTERVAL_MS) continue
                    lastSend = now

                    spectrum.compute(pcm, n, out)
                    onFrame(out)
                }
            } catch (_: InterruptedException) {
                // stop() 打断,正常退出
            }
        }
    }

    /** 留 4 帧余量,别让读取线程被调度抖动搞成 overrun */
    private fun bufferBytes(): Int {
        val min = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        val want = FRAME_SAMPLES * 2 * 4
        return if (min > 0) max(min, want) else want
    }
}
