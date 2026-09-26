package com.cyd.music

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * 频谱计算:PCM -> Hann 窗 -> FFT -> 对数频段 -> 自动增益 -> 0~255
 *
 * 纯计算,不碰任何 Android API(方便脱离手机单独验证)。
 * 频段按**频率**对数划分而不是按 bin 序号,换采样率不用改参数。
 */
class Spectrum(
    private val sampleRate: Int,
    private val fftSize: Int = 2048,
    private val bands: Int = 16
) {
    companion object {
        /** 频段覆盖范围:低于 40Hz 是隆隆声,高于 16kHz 基本听不见 */
        private const val F_LO = 40.0
        private const val F_HI = 16000.0

        /** 归一化 RMS 低于这个值算静音(约 -66dBFS) */
        const val SILENCE_RMS = 0.0005
    }

    private val n = fftSize
    private val re = DoubleArray(n)
    private val im = DoubleArray(n)

    /** Hann 窗,压低频谱泄漏 */
    private val win = DoubleArray(n) { 0.5 - 0.5 * cos(2.0 * PI * it / (n - 1)) }

    /** 位反转表(装填时顺手重排,省一趟循环) */
    private val rev = IntArray(n)

    /** 旋转因子表,避免在蝶形内层反复调三角函数 */
    private val cosT = DoubleArray(n / 2) { cos(-2.0 * PI * it / n) }
    private val sinT = DoubleArray(n / 2) { sin(-2.0 * PI * it / n) }

    /** 每个频段占用的 bin 区间 [lo, hi) */
    private val lo = IntArray(bands)
    private val hi = IntArray(bands)

    /** 每段幅度暂存,复用避免每帧分配 */
    private val mag = DoubleArray(bands)

    // 自动增益:快峰值 + 慢跟踪,任何音量下都饱满,又不忽大忽小
    private var agcPeak = 0.0
    private var agcStable = 0.0

    init {
        var bits = 0
        while ((1 shl bits) < n) bits++
        require((1 shl bits) == n) { "fftSize 必须是 2 的幂" }
        for (i in 0 until n) {
            var x = i
            var r = 0
            for (b in 0 until bits) {
                r = (r shl 1) or (x and 1)
                x = x shr 1
            }
            rev[i] = r
        }

        val binHz = sampleRate.toDouble() / n
        val maxBin = n / 2
        val span = F_HI / F_LO
        for (b in 0 until bands) {
            val f0 = F_LO * span.pow(b.toDouble() / bands)
            val f1 = F_LO * span.pow((b + 1.0) / bands)
            // 用四舍五入而不是截断:截断会把整条频段表系统性下移半个 bin,
            // 低频段本来只有一两个 bin 宽,一移就整段错位(实测 48Hz 会落到第 1 段)
            val a = max(1, Math.round(f0 / binHz).toInt())
            val z = min(maxBin, Math.round(f1 / binHz).toInt())
            lo[b] = a
            hi[b] = max(a + 1, z)   // 保证每段至少吃到一个 bin
        }
    }

    /** 换音源时清掉增益状态,免得新音源开头几帧被旧峰值压平 */
    fun reset() {
        agcPeak = 0.0
        agcStable = 0.0
    }

    /** 归一化均方根,用来判静音(0.0 ~ 1.0) */
    fun rms(pcm: ShortArray, count: Int): Double {
        if (count <= 0) return 0.0
        var acc = 0.0
        for (i in 0 until count) {
            val v = pcm[i] / 32768.0
            acc += v * v
        }
        return sqrt(acc / count)
    }

    /**
     * 算一帧频谱写进 out(长度必须等于 bands),值域 0~255。
     * pcm 为单声道样本,不足 fftSize 的部分补零。
     */
    fun compute(pcm: ShortArray, count: Int, out: IntArray) {
        val m = min(count, n)
        for (i in 0 until m) re[rev[i]] = pcm[i] / 32768.0 * win[i]
        for (i in m until n) re[rev[i]] = 0.0
        im.fill(0.0)

        fft()

        var frameMax = 0.0
        for (b in 0 until bands) {
            var mx = 0.0
            for (i in lo[b] until hi[b]) {
                val p = re[i] * re[i] + im[i] * im[i]
                if (p > mx) mx = p
            }
            val a = sqrt(mx)
            mag[b] = a
            if (a > frameMax) frameMax = a
        }

        // 峰值快跟随、慢衰减;再一阶低通稳住,约 1 秒时间常数
        agcPeak = max(agcPeak * 0.94, frameMax)
        if (agcPeak < 1e-4) agcPeak = 1e-4
        agcStable = if (agcStable <= 0.0) agcPeak else agcStable * 0.92 + agcPeak * 0.08

        for (b in 0 until bands) {
            val ratio = (mag[b] / agcStable).coerceIn(0.0, 1.0)
            // 开根号提升小幅度的可见度
            out[b] = (sqrt(ratio) * 255).toInt().coerceIn(0, 255)
        }
    }

    /** 迭代式 Cooley-Tukey,数据已在装填时按位反转排好 */
    private fun fft() {
        var len = 2
        while (len <= n) {
            val half = len shr 1
            val step = n / len
            var i = 0
            while (i < n) {
                var k = 0
                for (j in 0 until half) {
                    val wr = cosT[k]
                    val wi = sinT[k]
                    val p = i + j
                    val q = p + half
                    val xr = re[q]
                    val xi = im[q]
                    val vr = xr * wr - xi * wi
                    val vi = xr * wi + xi * wr
                    re[q] = re[p] - vr
                    im[q] = im[p] - vi
                    re[p] += vr
                    im[p] += vi
                    k += step
                }
                i += len
            }
            len = len shl 1
        }
    }
}
