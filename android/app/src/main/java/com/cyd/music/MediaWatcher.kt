package com.cyd.music

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.util.Log

/**
 * MediaSession 侧:播放状态、元数据兜底、以及板子按钮的媒体控制。
 *
 * 需要「通知使用权」—— `getActiveSessions` 要求调用方是已授权的通知监听器,
 * 所以 [MusicNotificationListener] 即使不解析通知也必须存在。
 */
class MediaWatcher(
    private val ctx: Context,
    private val handler: Handler
) {
    companion object {
        private const val TAG = "MediaWatcher"
    }

    private val component = ComponentName(ctx, MusicNotificationListener::class.java)
    private val msm = ctx.getSystemService(MediaSessionManager::class.java)

    @Volatile private var controller: MediaController? = null

    private val ctrlCb = object : MediaController.Callback() {
        override fun onMetadataChanged(md: MediaMetadata?) = readMetadata(md)

        override fun onPlaybackStateChanged(st: PlaybackState?) {
            NowPlaying.setPlaying(st?.state == PlaybackState.STATE_PLAYING)
        }

        override fun onSessionDestroyed() {
            handler.post { attachBest() }
        }
    }

    private val sessionsCb =
        MediaSessionManager.OnActiveSessionsChangedListener { attachBest() }

    /** 当前挂着的播放器包名,给界面显示 */
    val attachedPackage: String? get() = controller?.packageName

    fun start() {
        val m = msm ?: run { MusicBus.status("系统不支持 MediaSession"); return }
        try {
            m.addOnActiveSessionsChangedListener(sessionsCb, component)
            attachBest()
        } catch (e: SecurityException) {
            // 没给通知使用权时会抛这个
            Log.w(TAG, "addOnActiveSessionsChangedListener", e)
            MusicBus.status("缺少「通知使用权」,读不到播放器")
        }
    }

    fun stop() {
        try {
            msm?.removeOnActiveSessionsChangedListener(sessionsCb)
        } catch (_: Exception) {
        }
        detach()
    }

    /** 通知监听重新连上后，重新读取当前活动会话。 */
    fun refresh() = attachBest()

    private fun detach() {
        try {
            controller?.unregisterCallback(ctrlCb)
        } catch (_: Exception) {
        }
        controller = null
        MusicBus.sessionPackage = null
        MusicBus.sessionHasTrack = false
    }

    /**
     * 选出「正在播放」的那个会话。
     * 旧代码取 `controllers.lastOrNull()`,但这个顺序并不保证是正在播放的,
     * 手机上同时有多个会话(实测还有个 com.quark.browser)时会控制错对象。
     */
    private fun attachBest() {
        val m = msm ?: return
        val list = try {
            m.getActiveSessions(component)
        } catch (e: SecurityException) {
            Log.w(TAG, "getActiveSessions", e)
            return
        }
        val candidates = list.filter { it.packageName != ctx.packageName }
        val best = candidates.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: candidates.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PAUSED
        } ?: candidates.firstOrNull()

        if (best == null) {
            detach()
            NowPlaying.onSourceChanged()
            NowPlaying.setPlaying(false)
            return
        }
        if (best.sessionToken == controller?.sessionToken) {
            readMetadata(best.metadata)
            NowPlaying.setPlaying(best.playbackState?.state == PlaybackState.STATE_PLAYING)
            return
        }

        detach()
        controller = best
        MusicBus.sessionPackage = best.packageName
        // 换了播放器,让来源等级重新竞争
        NowPlaying.onSourceChanged()
        try {
            best.registerCallback(ctrlCb, handler)
        } catch (e: Exception) {
            Log.w(TAG, "registerCallback", e)
        }
        readMetadata(best.metadata)
        NowPlaying.setPlaying(best.playbackState?.state == PlaybackState.STATE_PLAYING)
        MusicBus.status("已挂上播放器 ${best.packageName}")
    }

    /**
     * 从 MediaSession 元数据里取歌名/歌手。
     *
     * 小米音乐把**歌词**放在 TITLE、把「歌名 - 歌手」放在 ARTIST(实测)。
     * 息屏后它的通知会冻结，但这两个 MediaSession 字段仍持续更新，所以
     * [Protocol.parseSessionFields] 会同时解析歌曲身份与当前歌词。
     */
    private fun readMetadata(md: MediaMetadata?) {
        md ?: return
        val subtitle = md.getText(MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE)?.toString()
            ?: md.getText(MediaMetadata.METADATA_KEY_ARTIST)?.toString()
            ?: md.getText(MediaMetadata.METADATA_KEY_ALBUM)?.toString()
        val title = md.getText(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)?.toString()
            ?: md.getText(MediaMetadata.METADATA_KEY_TITLE)?.toString()

        val fields = Protocol.parseSessionFields(title, subtitle, controller?.packageName) ?: return
        val hadTrack = MusicBus.sessionHasTrack
        MusicBus.sessionHasTrack = fields.song.isNotEmpty()
        Log.d(TAG, "媒体元数据: ${fields.song} - ${fields.artist}; 歌词=${fields.lyric}")
        if (fields.song.isNotEmpty()) {
            NowPlaying.setTrack(fields.song, fields.artist, NowPlaying.SRC_SESSION)
        } else if (hadTrack) {
            // Recover the identity before applying this live lyric.
            MusicNotificationListener.refresh(ctx)
        }
        if (fields.lyric.isNotEmpty()) NowPlaying.setLyric(fields.lyric)
    }

    /** 板子按钮 -> 媒体控制 */
    fun doAction(action: String) {
        val c = controller ?: run {
            MusicBus.status("没有正在运行的播放器")
            return
        }
        try {
            val tc = c.transportControls
            when (action) {
                "play" -> tc.play()
                "pause" -> tc.pause()
                "toggle" ->
                    if (c.playbackState?.state == PlaybackState.STATE_PLAYING) tc.pause()
                    else tc.play()
                "prev" -> tc.skipToPrevious()
                "next" -> tc.skipToNext()
                else -> Log.w(TAG, "未知按钮指令 $action")
            }
        } catch (e: Exception) {
            MusicBus.status("媒体控制失败: ${e.message}")
        }
    }
}
