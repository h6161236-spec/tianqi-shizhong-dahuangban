package com.cyd.music

/** 可直接由 Gradle JavaExec 运行的离线回归测试，不依赖 JUnit。 */
object VerifyMediaSync {

    @JvmStatic
    fun main(args: Array<String>) {
        val xiaomi = requireFields(
            Protocol.parseSessionFields(
                "狼牙月 伊人憔悴\n",
                "发如雪 - 未知歌手"
            )
        )
        check(xiaomi.song == "发如雪")
        check(xiaomi.artist == "未知歌手")
        check(xiaomi.lyric == "狼牙月 伊人憔悴")

        val normal = requireFields(Protocol.parseSessionFields("晴天", "周杰伦"))
        check(normal.song == "晴天")
        check(normal.artist == "周杰伦")
        check(normal.lyric.isEmpty())

        val duplicateTitle = requireFields(
            Protocol.parseSessionFields("发如雪", "发如雪 - 周杰伦")
        )
        check(duplicateTitle.lyric.isEmpty())

        // Redmi/HyperOS observed on 2026-09-25: the session's artist is
        // <unknown>; only the notification contains the actual track name.
        val lyricOnly = requireFields(Protocol.parseSessionFields(
            "歌唱也如梭,困住心如窝。\n", "<unknown>", "com.miui.player"
        ))
        check(lyricOnly.song.isEmpty())
        check(lyricOnly.artist.isEmpty())
        check(lyricOnly.lyric == "歌唱也如梭,困住心如窝。")
        val xiaomiArtistMetadata = requireFields(Protocol.parseSessionFields(
            "戏子多秋 可怜一处情深旧", "辞九门回忆 - 未知歌手", "com.miui.player"
        ))
        check(xiaomiArtistMetadata.song == "辞九门回忆")
        check(xiaomiArtistMetadata.artist == "未知歌手")
        check(xiaomiArtistMetadata.lyric == "戏子多秋 可怜一处情深旧")
        val ordinaryUnknown = requireFields(Protocol.parseSessionFields(
            "普通歌名", "<unknown>", "com.netease.cloudmusic"
        ))
        check(ordinaryUnknown.song == "普通歌名" && ordinaryUnknown.lyric.isEmpty())
        val longLyric = "歌词".repeat(35)
        check(Protocol.lrc(longLyric) == "LRC|$longLyric")
        check(Protocol.parseSessionFields(longLyric, "<unknown>", "com.miui.player")?.lyric == longLyric)
        check(Protocol.lrc("歌".repeat(100)).removePrefix("LRC|").toByteArray().size == 255)
        check(MediaSourcePolicy.notificationMaySetTrack("com.miui.player", false))
        check(!MediaSourcePolicy.notificationMaySetTrack("com.miui.player", true))
        check(!MediaSourcePolicy.notificationMaySetLyric(
            "com.miui.player", "com.miui.player", hasMiuiFocus = false
        ))

        check(!MediaSourcePolicy.notificationMaySetTrack("com.miui.player"))
        check(MediaSourcePolicy.notificationMaySetTrack(null))
        check(
            !MediaSourcePolicy.notificationMaySetLyric(
                "com.miui.player", "com.miui.player", hasMiuiFocus = true
            )
        )
        check(
            MediaSourcePolicy.notificationMaySetLyric(
                "com.netease.cloudmusic",
                "com.netease.cloudmusic",
                hasMiuiFocus = false
            )
        )
        check(
            !MediaSourcePolicy.notificationMaySetLyric(
                "com.miui.player",
                "com.netease.cloudmusic",
                hasMiuiFocus = true
            )
        )

        val sent = mutableListOf<String>()
        MusicBus.sender = { sent.add(it) }
        NowPlaying.clear()
        // Replay the phone's split sources, then several live lyric updates.
        NowPlaying.setTrack("我恨明月不照我", "未知歌手", NowPlaying.SRC_SESSION)
        NowPlaying.setLyric(lyricOnly.lyric)
        sent.clear()
        NowPlaying.setTrack("我恨明月不照我", "未知歌手", NowPlaying.SRC_SESSION)
        NowPlaying.setLyric(longLyric)
        NowPlaying.setLyric(longLyric)
        check(sent == listOf("LRC|$longLyric"))
        sent.clear()
        NowPlaying.resend()
        check(sent.take(2) == listOf("META|我恨明月不照我|未知歌手", "LRC|$longLyric"))
        NowPlaying.clear()
        MusicBus.sessionPackage = "com.miui.player"
        check(NowPlaying.summary() == "已检测到播放器，等待歌曲信息")
        sent.clear()
        NowPlaying.setLyric("通知断开时仍在更新的歌词")
        check(NowPlaying.summary().contains("通知断开时仍在更新的歌词"))
        check(!NowPlaying.summary().contains("未检测到播放器"))
        check(sent == listOf("META|等待歌名|", "LRC|通知断开时仍在更新的歌词"))
        sent.clear()
        NowPlaying.setLyric("下一句歌词")
        check(sent == listOf("LRC|下一句歌词"))
        sent.clear()
        NowPlaying.resend()
        check(sent.take(2) == listOf("META|等待歌名|", "LRC|下一句歌词"))
        sent.clear()
        NowPlaying.setTrack("落了白", "未知歌手", NowPlaying.SRC_SESSION)
        check(sent == listOf("META|落了白|未知歌手", "LRC|下一句歌词"))
        NowPlaying.onSourceChanged()
        check(!NowPlaying.summary().contains("落了白"))
        MusicBus.sessionPackage = null
        NowPlaying.clear()
        MusicBus.sender = null
        println("media sync regression checks passed")
    }

    private fun requireFields(fields: Protocol.SessionFields?): Protocol.SessionFields =
        fields ?: error("expected parsed session fields")
}
