package com.cyd.music

import android.app.Activity
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.util.Log

/**
 * 前台服务:把蓝牙、频谱采集、播放器监听串起来。
 *
 * 数据流:
 *   MediaSession / 通知  ->  NowPlaying(判重+仲裁)  ->  MusicBus.send  ->  BleClient
 *   SpectrumSource       ->  SPEC 行                                    ->  BleClient
 *   BleClient 收到 BTN|  ->  MediaWatcher.doAction                      ->  播放器
 *
 * 旧版直接在这里开 `Visualizer(0)` 做 FFT。那条路在这台手机上根本不出数
 * (session 0 需要 `CAPTURE_AUDIO_OUTPUT`,普通 App 拿不到),
 * 只使用用户授权的 App 音频，不提供其他采集模式。
 */
class MusicService : Service() {

    companion object {
        private const val TAG = "MusicService"
        private const val CHANNEL_ID = "cyd_music"
        private const val NOTI_ID = 1

        /** 由主界面传进来的录屏授权结果 */
        const val EXTRA_PROJECTION_RESULT = "projection_result"
        const val EXTRA_PROJECTION_DATA = "projection_data"

        /** 比板端 MUSIC_IDLE_MS 短，频谱静音时仍能证明播放器/服务活着。 */
        private const val PLAY_HEARTBEAT_MS = 3_000L
        private const val NOTIFICATION_REBIND_MS = 15_000L

        const val ACTION_START = "com.cyd.music.START"
        const val ACTION_STOP = "com.cyd.music.STOP"
    }

    private var ble: BleClient? = null
    private var mediaWatcher: MediaWatcher? = null
    private var spectrum: SpectrumSource? = null
    private var projection: MediaProjection? = null

    /** 媒体回调用独立线程,别占着主线程 */
    private val bgThread = HandlerThread("music-bg").apply { start() }
    private val bgHandler = Handler(bgThread.looper)
    private val mainHandler = Handler(android.os.Looper.getMainLooper())

    @Volatile private var started = false

    private val heartbeat = object : Runnable {
        override fun run() {
            if (!started) return
            NowPlaying.heartbeat()
            bgHandler.postDelayed(this, PLAY_HEARTBEAT_MS)
        }
    }

    /** HyperOS 可能在进程更新/回收后拒绝第一次自动绑定；允许自启动后持续补绑。 */
    private val notificationKeeper = object : Runnable {
        override fun run() {
            if (!started) return
            mediaWatcher?.refresh()
            MusicNotificationListener.refresh(this@MusicService)
            bgHandler.postDelayed(this, NOTIFICATION_REBIND_MS)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        // 必须先进前台再干活,否则 Android 12+ 会直接 ANR/抛异常
        startAsForeground(hasProjection = false)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopSelf()
            return START_NOT_STICKY
        }

        val code = intent?.getIntExtra(EXTRA_PROJECTION_RESULT, 0) ?: 0
        val data: Intent? = if (Build.VERSION.SDK_INT >= 33) {
            intent?.getParcelableExtra(EXTRA_PROJECTION_DATA, Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(EXTRA_PROJECTION_DATA)
        }
        if (started) return START_NOT_STICKY
        if (code != Activity.RESULT_OK || data == null) {
            stopSelf()
            return START_NOT_STICKY
        }
        started = true
        bootstrap(code, data)
        return START_NOT_STICKY
    }

    private fun bootstrap(code: Int, data: Intent) {
        projection = obtainProjection(code, data)
        if (projection == null) {
            stopSelf()
            return
        }
        MusicBus.serviceRunning = true
        MusicBus.sender = { line -> ble?.writeLine(line) }
        MusicBus.mediaAction = { action -> bgHandler.post { mediaWatcher?.doAction(action) } }
        MusicBus.notificationListenerChanged = { connected ->
            if (connected) bgHandler.post {
                mediaWatcher?.refresh()
                MusicNotificationListener.refresh(this)
            }
        }

        ble = BleClient(
            this,
            onStatus = { s -> MusicBus.status(s) },
            onData = { line -> onBleLine(line) }
        ).also { it.start() }

        mediaWatcher = MediaWatcher(this, bgHandler).also { it.start() }
        MusicNotificationListener.refresh(this)

        spectrum = SpectrumSource(this) { bands ->
            MusicBus.send(Protocol.spec(bands))
        }.also { it.start(projection!!) }

        bgHandler.removeCallbacks(heartbeat)
        bgHandler.post(heartbeat)
        bgHandler.removeCallbacks(notificationKeeper)
        bgHandler.post(notificationKeeper)

        MusicBus.status("服务已启动,频谱来源:${SpectrumSource.modeName(spectrum?.mode ?: 0)}")
    }

    private fun obtainProjection(code: Int, data: Intent): MediaProjection? {
        return try {
            startAsForeground(hasProjection = true)
            val mgr = getSystemService(MediaProjectionManager::class.java)
            mgr?.getMediaProjection(code, data)?.also { p ->
                // 用户在系统通知里点「停止投送」时会回调这里
                p.registerCallback(object : MediaProjection.Callback() {
                    override fun onStop() {
                        if (!started) return
                        MusicBus.status("投送已停止")
                        stopSelf()
                    }
                }, mainHandler)
            }
        } catch (e: Exception) {
            Log.w(TAG, "getMediaProjection 失败", e)
            null
        }
    }

    // ==================== 前台通知 ====================

    /**
     * 进前台。
     *
     * 拿到录屏令牌后必须**再调一次**并带上 `mediaProjection` 类型:
     * Android 14+ 要求捕获系统音频的服务声明这个类型,否则抛
     * SecurityException。而进前台又必须在拿令牌之前完成(否则
     * getMediaProjection 自己会失败),所以这里分两步走。
     */
    private fun startAsForeground(hasProjection: Boolean) {
        ensureChannel()

        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, MusicService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val openIntent = PendingIntent.getActivity(
            this, 1,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val noti: Notification = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("CYD 音乐同步")
            .setContentText(
                if (hasProjection) "正在同步歌词和音乐 App 音频" else "正在启动"
            )
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(openIntent)
            .setOngoing(true)
            .addAction(
                Notification.Action.Builder(
                    null as android.graphics.drawable.Icon?, "停止", stopIntent
                ).build()
            )
            .build()

        var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        if (hasProjection) {
            type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
        }
        startForeground(NOTI_ID, noti, type)
    }

    private fun ensureChannel() {
        val nm = getSystemService(NotificationManager::class.java) ?: return
        if (nm.getNotificationChannel(CHANNEL_ID) != null) return
        val ch = NotificationChannel(
            CHANNEL_ID, "CYD 音乐同步", NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "保持蓝牙连接与频谱推送"
            setShowBadge(false)
        }
        nm.createNotificationChannel(ch)
    }

    /** 板子回传的指令(目前只有按钮:BTN|play/prev/next) */
    private fun onBleLine(line: String) {
        if (line.startsWith("BTN|")) {
            val action = line.removePrefix("BTN|").trim()
            Log.i(TAG, "收到板端按钮: $action")
            bgHandler.post { mediaWatcher?.doAction(action) }
        }
    }

    override fun onDestroy() {
        started = false
        MusicBus.serviceRunning = false
        MusicBus.sender = null
        MusicBus.mediaAction = null
        MusicBus.notificationListenerChanged = null
        MusicBus.sessionPackage = null
        bgHandler.removeCallbacks(heartbeat)
        bgHandler.removeCallbacks(notificationKeeper)

        spectrum?.stop()
        spectrum = null
        mediaWatcher?.stop()
        mediaWatcher = null
        ble?.stop()
        ble = null
        try {
            projection?.stop()
        } catch (_: Exception) {
        }
        projection = null
        NowPlaying.clear()
        bgThread.quitSafely()
        MusicBus.status("服务已停止")
        super.onDestroy()
    }
}
