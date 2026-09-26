package com.cyd.music

import org.json.JSONObject

/**
 * BLE 行协议的组装与字段清洗。
 *
 * 协议是「一行一条、`\n` 结尾、`|` 分隔」,所以字段里**绝对不能**出现 `\n` 或 `|`。
 * 实测小米音乐的歌词字段自带结尾换行(`android.title` = "尘入深深所说\n"),
 * 不洗掉会把一行拆成两行,板子那边直接解析错位。
 */
object Protocol {

    data class SessionFields(
        val song: String,
        val artist: String,
        val lyric: String
    )

    /** Metadata fits the firmware's 128-byte fields; lyrics use 256 bytes. */
    private const val MAX_FIELD_BYTES = 120
    const val MAX_LYRIC_BYTES = 255

    /**
     * 洗掉换行 / 竖线 / 控制字符,并按 UTF-8 字节数截断(不切碎多字节字符)。
     */
    fun clean(raw: String?, maxBytes: Int = MAX_FIELD_BYTES): String {
        if (raw.isNullOrEmpty()) return ""
        val sb = StringBuilder(raw.length)
        for (ch in raw) {
            when {
                ch == '|' -> sb.append('/')             // 和分隔符冲突,换个字符保留可读性
                ch == '\n' || ch == '\r' -> sb.append(' ')
                ch.code < 0x20 || ch.code == 0x7F -> {}  // 其它控制字符丢掉
                else -> sb.append(ch)
            }
        }
        return truncateUtf8(sb.toString().trim(), maxBytes)
    }

    /** 按 UTF-8 字节上限截断,保证不在多字节字符中间切开 */
    private fun truncateUtf8(s: String, maxBytes: Int): String {
        var bytes = 0
        for ((i, ch) in s.withIndex()) {
            val n = when {
                ch.code < 0x80 -> 1
                ch.code < 0x800 -> 2
                Character.isHighSurrogate(ch) -> 4   // 代理对,连着的低位一起算
                Character.isLowSurrogate(ch) -> 0
                else -> 3
            }
            if (bytes + n > maxBytes) return s.substring(0, i)
            bytes += n
        }
        return s
    }

    // ==================== 协议行组装 ====================

    /** `META|歌名|歌手` —— 切歌 */
    fun meta(title: String?, artist: String?) = "META|${clean(title)}|${clean(artist)}"

    /** `LRC|当前歌词行` */
    fun lrc(line: String?) = "LRC|${clean(line, MAX_LYRIC_BYTES)}"

    /** `PLAY|1` / `PLAY|0` —— 播放中 / 已暂停(让板子能区分"暂停"和"App 掉线") */
    fun play(playing: Boolean) = "PLAY|${if (playing) 1 else 0}"

    /** `SPEC|v0,...,v15` —— 16 段能量 0~255 */
    fun spec(vals: IntArray): String {
        val sb = StringBuilder(5 + vals.size * 4).append("SPEC|")
        for (i in vals.indices) {
            if (i > 0) sb.append(',')
            sb.append(vals[i].coerceIn(0, 255))
        }
        return sb.toString()
    }

    // ==================== 歌名 / 歌手 解析 ====================

    /**
     * 从小米灵动岛参数里取干净歌名。
     * 实测 `miui.focus.param.media` =
     *   `{"param_v2":{"param_island":{"shareData":{"title":"借过一下",...}}}}`
     * 比去猜 "歌名 - 歌手" 怎么拆可靠得多。
     */
    fun miuiFocusTitle(json: String?): String? {
        if (json.isNullOrBlank()) return null
        return try {
            val t = JSONObject(json)
                .optJSONObject("param_v2")
                ?.optJSONObject("param_island")
                ?.optJSONObject("shareData")
                ?.optString("title")
            if (t.isNullOrBlank()) null else t
        } catch (_: Exception) {
            null
        }
    }

    /**
     * 拆 "歌名 - 歌手"。
     * 用**最后一个** " - " 断开,因为歌名里带横杠比歌手里带横杠常见得多。
     * 拆不开就整串当歌名。
     */
    fun splitTitleArtist(s: String): Pair<String, String> {
        val i = s.lastIndexOf(" - ")
        if (i <= 0) return s to ""
        return s.substring(0, i) to s.substring(i + 3)
    }

    /**
     * 解析 MediaSession 的标题与副标题。
     *
     * 小米音乐在 HyperOS 上会把当前歌词放进 title，把「歌名 - 歌手」放进
     * subtitle；即使息屏后通知完全冻结，这两个 MediaSession 字段仍持续更新。
     * 普通播放器则通常是 title=歌名、subtitle=歌手，此时不把 title 当歌词。
     */
    fun parseSessionFields(
        rawTitle: String?, rawSubtitle: String?, playerPackage: String? = null
    ): SessionFields? {
        val title = clean(rawTitle, MAX_LYRIC_BYTES)
        val subtitle = clean(rawSubtitle)

        if (subtitle.contains(" - ")) {
            val (rawSong, rawArtist) = splitTitleArtist(subtitle)
            val song = clean(rawSong)
            if (song.isEmpty()) return null
            val artist = clean(rawArtist)
            val lyric = title.takeIf {
                it.isNotEmpty() && it != song && it != artist && it != subtitle
            }.orEmpty()
            return SessionFields(song, artist, lyric)
        }

        // HyperOS sometimes puts "song - artist" in ARTIST and exposes the
        // current lyric as TITLE, instead of filling DISPLAY_SUBTITLE.
        if (playerPackage == "com.miui.player" && subtitle.isNotEmpty() &&
            subtitle != "<unknown>" && subtitle != "unknown") {
            val (rawSong, rawArtist) = splitTitleArtist(subtitle)
            val song = clean(rawSong)
            if (song.isNotEmpty() && song != "<unknown>" && song != "unknown") {
                return SessionFields(song, clean(rawArtist), title)
            }
        }

        if (title.isEmpty()) return null
        // Some HyperOS versions expose title=lyric, artist=<unknown>.
        // The notification supplies the song identity; never promote this
        // changing lyric to META or every line will clear the lyric window.
        if (playerPackage == "com.miui.player") return SessionFields("", "", title)
        return SessionFields(clean(title), subtitle, "")
    }

    /**
     * 已知干净歌名时,把它从 "歌名 - 歌手" 里剔掉,剩下的就是歌手。
     */
    fun artistAfterTitle(combined: String, knownTitle: String): String {
        val prefix = "$knownTitle - "
        if (combined.startsWith(prefix)) return combined.substring(prefix.length)
        return splitTitleArtist(combined).second
    }
}
