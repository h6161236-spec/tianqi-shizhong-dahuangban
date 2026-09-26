package com.cyd.music

import android.app.Notification
import android.content.ComponentName
import android.content.Context
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log

/**
 * 通知监听:从播放器通知里提取 歌名 / 歌手 / 逐句歌词。
 *
 * 两家播放器的字段布局完全不同,所以不能写死:
 *
 *   小米音乐(实测 HyperOS 3):
 *     android.title = 逐句歌词(且**自带结尾换行**)
 *     android.text  = "歌名 - 歌手"
 *     tickerText    = null
 *     miui.focus.param.media = {"...":{"shareData":{"title":"歌名"}}}   <- 干净歌名
 *
 *   多数第三方播放器(如网易云):
 *     android.title = 歌名
 *     android.text  = 歌手
 *     tickerText    = 逐句歌词(开了「状态栏歌词」才有)
 *
 * 所以先定「歌曲身份」,再把**剩下那个会变的文本字段**当歌词。
 * 这样两种布局都能落地,不用为每个播放器写分支。
 */
class MusicNotificationListener : NotificationListenerService() {

    companion object {
        private const val TAG = "MusicNotiListener"
        /** 小米灵动岛参数,里面有不含歌手的干净歌名 */
        private const val MIUI_FOCUS = "miui.focus.param.media"

        @Volatile private var live: MusicNotificationListener? = null

        /**
         * 已连接时立即重读当前播放器通知；只有系统授权、监听器尚未绑定时请求重绑。
         * 这解决 HyperOS 显示“已授权”但强杀后监听器实际上没有运行的问题。
         */
        fun refresh(context: Context) {
            val listener = live
            if (listener != null) {
                listener.publishActiveNotifications()
            } else {
                try {
                    requestRebind(ComponentName(context, MusicNotificationListener::class.java))
                } catch (e: Exception) {
                    Log.w(TAG, "requestRebind", e)
                }
            }
        }

        fun isConnected(): Boolean = live != null

        /**
         * 兜底提示:通知不是 MediaStyle 模板的播放器靠包名认。
         * 正常情况下靠结构判断就够了,这个表只是补漏,不用维护得很全。
         */
        private val KNOWN_PLAYERS = setOf(
            "com.miui.player",
            "com.netease.cloudmusic",
            "com.tencent.qqmusic",
            "com.kugou.android",
            "cn.kuwo.player",
            "com.spotify.music"
        )
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        live = this
        MusicBus.notificationAccess = true
        Log.i(TAG, "通知监听已连接")
        MusicBus.notificationListenerChanged?.invoke(true)
        publishActiveNotifications()
    }

    override fun onListenerDisconnected() {
        if (live === this) live = null
        MusicBus.notificationAccess = false
        Log.w(TAG, "通知监听已断开")
        MusicBus.notificationListenerChanged?.invoke(false)
        super.onListenerDisconnected()
        try {
            requestRebind(ComponentName(this, MusicNotificationListener::class.java))
        } catch (e: Exception) {
            Log.w(TAG, "requestRebind after disconnect", e)
        }
    }

    override fun onDestroy() {
        if (live === this) live = null
        MusicBus.notificationAccess = false
        MusicBus.notificationListenerChanged?.invoke(false)
        super.onDestroy()
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        processNotification(sbn ?: return)
    }

    /** 监听器重新绑定时，补读当前仍在通知栏里的歌名和歌词。 */
    private fun publishActiveNotifications() {
        if (!MusicBus.serviceRunning) return
        val current = try {
            activeNotifications?.toList().orEmpty()
        } catch (e: Exception) {
            Log.w(TAG, "activeNotifications", e)
            emptyList()
        }
        current.forEach(::processNotification)
    }

    private fun processNotification(sbn: StatusBarNotification) {
        val n = sbn.notification
        if (!MusicBus.serviceRunning) return
        if (!isMediaNotification(sbn, n)) return

        val ex = n.extras ?: return
        fun str(key: String) = Protocol.clean(
            ex.getCharSequence(key)?.toString(), Protocol.MAX_LYRIC_BYTES
        )

        val title = str(Notification.EXTRA_TITLE)
        val text = str(Notification.EXTRA_TEXT)
        val ticker = Protocol.clean(n.tickerText?.toString(), Protocol.MAX_LYRIC_BYTES)
        val bigText = str(Notification.EXTRA_BIG_TEXT)
        val subText = str(Notification.EXTRA_SUB_TEXT)
        val focusTitle = Protocol.clean(
            Protocol.miuiFocusTitle(ex.getString(MIUI_FOCUS))
        )
        val sessionPackage = MusicBus.sessionPackage
        if (sessionPackage != null && sbn.packageName != sessionPackage) return
        val notificationMaySetTrack =
            MediaSourcePolicy.notificationMaySetTrack(sessionPackage, MusicBus.sessionHasTrack)
        // A fallback identity must not outrank a later complete session.
        val focusPriority = if (sessionPackage == "com.miui.player")
            NowPlaying.SRC_SESSION else NowPlaying.SRC_FOCUS

        Log.d(TAG, "媒体通知: pkg=${sbn.packageName}, title=$title, text=$text, focus=$focusTitle")

        // ---------- 第一步:定歌曲身份 ----------
        // 先认灵动岛 JSON 里的干净歌名(实测最准),再退回文本里拆。
        var combo = ""
        when {
            focusTitle.isNotEmpty() -> {
                // text 是「歌名 - 歌手」,把歌名切掉剩下的就是歌手
                var artist = ""
                if (text.contains(" - ")) {
                    combo = text
                    artist = Protocol.artistAfterTitle(text, focusTitle)
                }
                if (notificationMaySetTrack) {
                    NowPlaying.setTrack(focusTitle, artist, focusPriority)
                }
            }

            text.contains(" - ") && notificationMaySetTrack -> {
                // 没有 MediaSession 兜底时才靠猜:「歌名 - 歌手」塞在正文里
                combo = text
                val (s, a) = Protocol.splitTitleArtist(text)
                NowPlaying.setTrack(s, a, if (sessionPackage == "com.miui.player")
                    NowPlaying.SRC_SESSION else NowPlaying.SRC_NOTI)
            }

            notificationMaySetTrack && title.isNotEmpty() && sessionPackage != "com.miui.player" -> {
                // 最常规的布局:标题是歌名,正文是歌手
                NowPlaying.setTrack(title, text, NowPlaying.SRC_NOTI)
            }
        }

        // ---------- 第二步:剩下那个会变的文本字段就是歌词 ----------
        // 小米音乐的歌词在 android.title,第三方播放器一般在 tickerText。
        // 不用判断是哪家:凡是和歌名/歌手/拼接串都不相等的,就是歌词。
        // (setLyric 内部还会再挡一层歌名/歌手重复)
        val exclude = setOf(combo, focusTitle, text, subText).filter { it.isNotEmpty() }
        if (MediaSourcePolicy.notificationMaySetLyric(
                sbn.packageName, sessionPackage, focusTitle.isNotEmpty()
            )) {
            val lyric = listOf(ticker, title, bigText)
                .firstOrNull { it.isNotEmpty() && it !in exclude }
            if (lyric != null) NowPlaying.setLyric(lyric)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        val n = sbn?.notification ?: return
        if (!isMediaNotification(sbn, n)) return
        // 播放器通知消失 = 退出播放。清空后板子会在 MUSIC_IDLE_MS 后回到时钟。
        NowPlaying.onSourceChanged()
        NowPlaying.setPlaying(false)
    }

    /**
     * 是不是播放器通知。
     *
     * 优先靠结构判断:带 MediaSession token / MediaStyle 模板 / transport 分类
     * 的通知就是播放器发的。这比维护包名白名单可靠 —— 换个播放器就不用改代码。
     * [KNOWN_PLAYERS] 只用来补那些不走 MediaStyle 的漏。
     */
    private fun isMediaNotification(sbn: StatusBarNotification, n: Notification): Boolean {
        val ex = n.extras
        if (ex != null) {
            if (ex.containsKey(Notification.EXTRA_MEDIA_SESSION)) return true
            if (ex.containsKey(MIUI_FOCUS)) return true
            val template = ex.getString(Notification.EXTRA_TEMPLATE)
            if (template != null && template.contains("MediaStyle")) return true
        }
        if (n.category == Notification.CATEGORY_TRANSPORT) return true
        return sbn.packageName in KNOWN_PLAYERS
    }
}
