package com.cyd.music

/**
 * 当前播放状态的唯一真相,负责**去重**、**来源仲裁**和按需下发。
 *
 * 存在的理由:歌名/歌词/播放状态来自两个数据源(通知监听 + MediaSession),
 * 两边都会高频回调。旧代码直接在回调里拼 `META|title|text` 就发,
 * 而小米音乐的通知 title 装的是**逐句歌词**,于是每换一句歌词就重发一次 META,
 * 板子上的歌名一直在跳。
 *
 * 光去重还不够:两个数据源对「歌名是什么」的结论可能不一致,
 * 谁后到谁说话就会来回打架(每次都重发 META 并清空歌词区)。
 * 所以 setTrack 带来源等级,只认当前最可靠的那个:
 *
 *   [SRC_FOCUS]   小米灵动岛 JSON 里的干净歌名 —— 实测最准,不含歌手
 *   [SRC_SESSION] MediaSession 结构化元数据 —— 通用播放器靠它
 *   [SRC_NOTI]    通知文本拆出来的 —— 猜的,只在前两者都没有时用
 *
 * 播放器换了(会话易主 / 通知消失)时调 [onSourceChanged] 把等级放开,
 * 否则从小米音乐切到别的播放器后,新播放器的低等级数据会被永久挡住。
 */
object NowPlaying {

    /** 通知文本拆出来的,可靠性最低 */
    const val SRC_NOTI = 1
    /** MediaSession 结构化元数据 */
    const val SRC_SESSION = 2
    /** 小米灵动岛 JSON,实测最准 */
    const val SRC_FOCUS = 3

    @Volatile private var song = ""
    @Volatile private var artist = ""
    @Volatile private var lyric = ""
    @Volatile private var playing = false
    private var pendingTrackSent = false

    /** 当前歌曲身份是哪个等级的来源定的 */
    @Volatile private var bestPriority = 0

    /**
     * 换歌 —— 只在歌名或歌手真的变了时发 META,并清掉上一首的残留歌词。
     * @param priority 见 [SRC_FOCUS] / [SRC_SESSION] / [SRC_NOTI]
     */
    @Synchronized
    fun setTrack(rawSong: String?, rawArtist: String?, priority: Int) {
        val s = Protocol.clean(rawSong)
        if (s.isEmpty()) return
        val a = Protocol.clean(rawArtist)

        // 有更可靠的来源在管这首歌,低等级的直接忽略。
        // 小米音乐把逐句歌词放在 MediaSession 的 TITLE 里,就是靠这条挡掉的。
        if (priority < bestPriority) return

        if (s == song && a == artist) {
            if (priority > bestPriority) bestPriority = priority
            return
        }

        val resolvingTrack = song.isEmpty() && lyric.isNotEmpty()
        bestPriority = priority
        song = s
        artist = a
        pendingTrackSent = false
        if (!resolvingTrack) lyric = ""
        MusicBus.send(Protocol.meta(song, artist))
        MusicBus.send(Protocol.lrc(lyric))
    }

    /** 更新歌词行 —— 和歌名/歌手重复的内容不算歌词 */
    @Synchronized
    fun setLyric(rawLyric: String?) {
        val l = Protocol.clean(rawLyric, Protocol.MAX_LYRIC_BYTES)
        if (l.isEmpty() || l == lyric) return
        if (l == song || l == artist) return
        // 「歌名 - 歌手」这种拼好的整串也不是歌词
        if (song.isNotEmpty() && l == "$song - $artist") return
        lyric = l
        // The firmware needs a META before it opens its lyric window. A
        // temporarily unavailable notification must not hide live lyrics.
        if (song.isEmpty() && !pendingTrackSent) {
            MusicBus.send(Protocol.meta("等待歌名", ""))
            pendingTrackSent = true
        }
        MusicBus.send(Protocol.lrc(lyric))
    }

    /** 播放/暂停 —— 让板子能区分「暂停了」和「App 掉线了」 */
    @Synchronized
    fun setPlaying(p: Boolean) {
        if (p == playing) return
        playing = p
        MusicBus.send(Protocol.play(p))
    }

    /**
     * 数据来源变了(换播放器 / 通知消失),把来源等级放开重新竞争。
     * 不做这件事的话,从小米音乐切到别的播放器后新数据会被一直挡住。
     */
    @Synchronized
    fun onSourceChanged() {
        bestPriority = 0
        song = ""
        artist = ""
        lyric = ""
        pendingTrackSent = false
    }

    /**
     * 蓝牙重连后把当前状态整套重发一次。
     * 不做这件事的话,重连后板子会一直空着,直到下次切歌才有内容。
     */
    @Synchronized
    fun resend() {
        if (song.isNotEmpty()) MusicBus.send(Protocol.meta(song, artist))
        else if (lyric.isNotEmpty()) MusicBus.send(Protocol.meta("等待歌名", ""))
        if (lyric.isNotEmpty()) MusicBus.send(Protocol.lrc(lyric))
        MusicBus.send(Protocol.play(playing))
    }

    /**
     * 周期播放心跳。频谱可能因为静音而没有数据，板子不能再把这等同于播放结束。
     * PLAY 行在 BLE 队列里会自动合并，只保留最新状态，不会越积越多。
     */
    @Synchronized
    fun heartbeat() {
        MusicBus.send(Protocol.play(playing))
    }

    /** 播放器退出时清空,下次播放会被当成新歌重新下发 */
    @Synchronized
    fun clear() {
        song = ""
        artist = ""
        lyric = ""
        playing = false
        bestPriority = 0
        pendingTrackSent = false
    }

    val isPlaying: Boolean get() = playing

    /** 给界面显示用的一行摘要 */
    @Synchronized
    fun summary(): String = when {
        song.isEmpty() && lyric.isNotEmpty() -> "已检测到播放器，等待歌名\n$lyric"
        song.isEmpty() && MusicBus.sessionPackage != null -> "已检测到播放器，等待歌曲信息"
        song.isEmpty() -> "未检测到播放器"
        lyric.isEmpty() -> "$song - $artist"
        else -> "$song - $artist\n$lyric"
    }
}
