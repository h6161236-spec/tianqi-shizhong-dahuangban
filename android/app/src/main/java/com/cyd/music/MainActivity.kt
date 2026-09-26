package com.cyd.music

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.View
import android.view.WindowInsets
import android.widget.Button
import android.widget.TextView
import android.widget.Toast

/**
 * 主界面。
 *
 * 启动一次要过 4 道基础关卡:运行时权限 -> 蓝牙开关 -> 通知使用权 -> 具体 App 投屏。
 * 应用只请求用户选择的音乐 App 投屏，不自动请求默认显示或整个屏幕。
 * 每一关都是异步的(回调或跳设置页),所以写成可续跑的状态机:
 * [proceed] 从头检查一遍,遇到没过的那关就发起请求,回来再调 [proceed]。
 *
 * 旧版没有 `onRequestPermissionsResult`,用户授权完界面毫无反应；
 * 现在授权返回后和首次打开 App 时都会自动续跑启动流程。
 *
 * 提供桌面快捷方式创建功能，用户可以通过快捷方式一键启动同步。
 */
class MainActivity : Activity() {

    companion object {
        private const val REQ_PERMS = 1
        private const val REQ_BT_ENABLE = 2
        private const val REQ_PROJECTION = 3
    }

    private val handler = Handler(Looper.getMainLooper())
    private lateinit var tvStatus: TextView
    private lateinit var tvTips: TextView
    private lateinit var tvNow: TextView

    /** 自动启动或用户点过启动按钮，流程进行中 —— 各道关卡回来后继续 */
    private var launching = false
    /**
     * 有一个关卡的请求还没回来。
     *
     * 必须有这个闸门:`onActivityResult` 比 `onResume` **先**执行,
     * 两边都会调 [proceed],不挡一下就会连开两个授权弹窗/设置页。
     */
    private var awaitingResult = false

    /** 通知使用权那关没有回调,一次 launching 里只自动跳一次设置页 */
    private var notiSettingsOpened = false

    // 类型必须写出来:handler.post 返回 Boolean,不标注的话整个 lambda
    // 会被推成 (String) -> Boolean,跟 statusReporter 的 (String) -> Unit 对不上。
    private val statusUpdater: (String) -> Unit = { s ->
        handler.post { if (!isFinishing) tvStatus.text = s }
    }

    /** 每秒刷一次「正在播放」和按钮状态 */
    private val ticker = object : Runnable {
        override fun run() {
            tvNow.text = NowPlaying.summary()
            refreshTips()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Android 15+ lays out targetSdk 35 apps edge-to-edge. Keep the title
        // and controls below the status bar / display cutout (Dynamic Island).
        val root = findViewById<View>(R.id.rootScroll)
        val baseLeft = root.paddingLeft
        val baseTop = root.paddingTop
        val baseRight = root.paddingRight
        val baseBottom = root.paddingBottom
        root.setOnApplyWindowInsetsListener { view, insets ->
            val safe = if (Build.VERSION.SDK_INT >= 30) {
                insets.getInsets(
                    WindowInsets.Type.statusBars() or
                        WindowInsets.Type.displayCutout() or
                        WindowInsets.Type.navigationBars()
                )
            } else {
                @Suppress("DEPRECATION")
                android.graphics.Insets.of(
                    insets.systemWindowInsetLeft,
                    insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight,
                    insets.systemWindowInsetBottom
                )
            }
            view.setPadding(
                baseLeft + safe.left,
                baseTop + safe.top,
                baseRight + safe.right,
                baseBottom + safe.bottom
            )
            insets
        }
        root.requestApplyInsets()

        tvStatus = findViewById(R.id.tvStatus)
        tvTips = findViewById(R.id.tvTips)
        tvNow = findViewById(R.id.tvNow)
        MusicBus.statusReporter = statusUpdater

        findViewById<Button>(R.id.btnRealSpectrum).setOnClickListener {
            beginLaunch()
        }
        findViewById<Button>(R.id.btnCreateShortcut).setOnClickListener {
            createShortcut()
        }
        findViewById<Button>(R.id.btnStop).setOnClickListener { stopSending() }
        findViewById<Button>(R.id.btnNotiPerm).setOnClickListener {
            openNotificationSettings()
        }

        MusicBus.status(if (MusicBus.serviceRunning) "服务运行中" else "未启动")
    }

    override fun onResume() {
        super.onResume()
        handler.post(ticker)
        if (notificationListenerEnabled()) MusicNotificationListener.refresh(this)
        // 从设置页回来时继续未走完的流程(通知使用权那关没有回调)。
        // awaitingResult 挡住「弹窗还没关就又跑一遍」的情况。
        if (launching && !awaitingResult) {
            proceed()
        }
    }

    override fun onPause() {
        handler.removeCallbacks(ticker)
        super.onPause()
    }

    override fun onDestroy() {
        if (MusicBus.statusReporter === statusUpdater) MusicBus.statusReporter = null
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    // ==================== 启动流程 ====================

    private fun beginLaunch() {
        launching = true
        if (MusicBus.serviceRunning) {
            // 允许重新选择音乐 App。旧服务持有的投屏令牌必须先释放，
            // 否则系统选择器不会再次出现，按钮会表现为没有反应。
            stopService(Intent(this, MusicService::class.java))
            MusicBus.status("正在停止旧同步...")
            handler.postDelayed({
                if (launching && !isFinishing) proceed()
            }, 350L)
            return
        }
        notiSettingsOpened = false
        awaitingResult = false
        proceed()
    }

    /**
     * 从头把 2 道关卡检一遍,停在第一道没过的关上。
     * 每次回调都重新调它,不用记「上次走到哪」。
     */
    private fun proceed() {
        if (!launching) return

        // 关卡 1:运行时权限
        val needed = requiredPermissions().filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (needed.isNotEmpty()) {
            MusicBus.status("请授予蓝牙和通知权限")
            awaitingResult = true
            requestPermissions(needed.toTypedArray(), REQ_PERMS)
            return
        }

        // 关卡 2:蓝牙开关
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null) {
            launching = false
            MusicBus.status("这台设备没有蓝牙")
            return
        }
        if (!adapter.isEnabled) {
            MusicBus.status("请打开蓝牙")
            try {
                awaitingResult = true
                startActivityForResult(
                    Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_BT_ENABLE
                )
            } catch (_: Exception) {
                awaitingResult = false
                MusicBus.status("请手动打开蓝牙后重试")
                launching = false
            }
            return
        }

        // 关卡 3:MediaSession 读取和通知歌词兜底都需要通知使用权。
        if (!notificationListenerEnabled()) {
            if (!notiSettingsOpened) {
                notiSettingsOpened = true
                MusicBus.status("请在列表里勾选「CYD歌词监听」,然后返回")
                openNotificationSettings()
            } else {
                // 已经跳过一次了,别再抢用户的界面
                launching = false
                MusicBus.status("还没拿到「通知使用权」,授权后再点一次「开始发送」")
            }
            return
        }

        val mgr = getSystemService(MediaProjectionManager::class.java)
        if (mgr == null || Build.VERSION.SDK_INT < 34) {
            launching = false
            MusicBus.status("选择单个 App 需要 Android 14 或更新版本")
            return
        }
        MusicBus.status("请选择音乐 App 并确认，不要选择整个屏幕")
        try {
            awaitingResult = true
            startActivityForResult(mgr.createScreenCaptureIntent(
                MediaProjectionConfig.createConfigForUserChoice()
            ), REQ_PROJECTION)
        } catch (_: Exception) {
            awaitingResult = false
            launching = false
            MusicBus.status("无法打开 App 选择，请重试")
        }
    }

    private fun startService(data: Intent, resultCode: Int) {
        val i = Intent(this, MusicService::class.java).setAction(MusicService.ACTION_START)
        i.putExtra(MusicService.EXTRA_PROJECTION_RESULT, resultCode)
        i.putExtra(MusicService.EXTRA_PROJECTION_DATA, data)
        startForegroundService(i)
        MusicBus.status("正在启动 ...")
    }

    private fun stopSending() {
        launching = false
        stopService(Intent(this, MusicService::class.java))
        MusicBus.status("已停止")
    }

    // ==================== 各关卡的回调 ====================

    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<out String>, grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_PERMS) return
        awaitingResult = false

        // 录音被拒只影响频谱,蓝牙被拒就完全没法用了
        val denied = permissions.filterIndexed { i, _ ->
            grantResults.getOrNull(i) != PackageManager.PERMISSION_GRANTED
        }
        val btDenied = denied.any {
            it == Manifest.permission.BLUETOOTH_SCAN ||
                it == Manifest.permission.BLUETOOTH_CONNECT ||
                it == Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (btDenied) {
            launching = false
            MusicBus.status("蓝牙权限被拒,无法连接板子。可到系统设置里手动开启")
            return
        }
        if (denied.contains(Manifest.permission.RECORD_AUDIO)) {
            launching = false
            MusicBus.status("未授权音频采集，未启动同步")
            return
        }
        // 授权完自动往下走,不用用户再点一次
        proceed()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_BT_ENABLE || requestCode == REQ_PROJECTION) {
            awaitingResult = false
        }
        when (requestCode) {
            REQ_BT_ENABLE -> {
                if (resultCode != RESULT_OK) {
                    launching = false
                    MusicBus.status("蓝牙未打开")
                } else {
                    proceed()
                }
            }

            REQ_PROJECTION -> {
                if (resultCode == RESULT_OK && data != null) {
                    launching = false
                    startService(data, resultCode)
                } else {
                    launching = false
                    MusicBus.status("已取消，未启动同步")
                }
            }
        }
    }

    // ==================== 桌面快捷方式 ====================

    private fun createShortcut() {
        if (Build.VERSION.SDK_INT < 26) {
            Toast.makeText(this, "需要 Android 8.0 及以上版本", Toast.LENGTH_SHORT).show()
            return
        }

        val shortcutManager = getSystemService(ShortcutManager::class.java)
        if (shortcutManager == null || !shortcutManager.isRequestPinShortcutSupported) {
            Toast.makeText(this, "当前设备不支持创建快捷方式", Toast.LENGTH_SHORT).show()
            return
        }

        val shortcut = ShortcutInfo.Builder(this, "quick_launch")
            .setShortLabel("音乐投屏")
            .setLongLabel("快速启动音乐投屏")
            .setIcon(Icon.createWithResource(this, android.R.drawable.stat_sys_data_bluetooth))
            .setIntent(Intent(this, QuickLaunchActivity::class.java)
                .setAction(Intent.ACTION_VIEW)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            .build()

        try {
            shortcutManager.requestPinShortcut(shortcut, null)
            Toast.makeText(this, "快捷方式已添加到桌面", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "创建快捷方式失败: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    // ==================== 权限工具 ====================

    private fun requiredPermissions(): List<String> {
        val list = mutableListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= 31) {
            list.add(Manifest.permission.BLUETOOTH_SCAN)
            list.add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            // 29~30:BLE 扫描要位置权限
            list.add(Manifest.permission.ACCESS_COARSE_LOCATION)
            list.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            list.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        return list
    }

    private fun notificationListenerEnabled(): Boolean {
        val flat = Settings.Secure.getString(
            contentResolver, "enabled_notification_listeners"
        ) ?: return false
        return flat.split(':').any {
            it.startsWith("$packageName/") || it.contains("$packageName/")
        }
    }

    private fun openNotificationSettings() {
        try {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        } catch (_: Exception) {
            MusicBus.status("打不开通知设置页,请手动到「设置-通知-通知使用权」授权")
        }
    }

    private fun refreshTips() {
        val noti = when {
            !notificationListenerEnabled() -> "未授权"
            MusicNotificationListener.isConnected() -> "监听已连接"
            else -> "授权已开但监听未连接"
        }
        val svc = if (MusicBus.serviceRunning) "运行中" else "未启动"
        val conn = MusicBus.sessionPackage?.let { "已挂上 $it" } ?: "未检测到播放器"

        tvTips.text = """
            服务:$svc    通知使用权:$noti
            播放器:$conn

            使用步骤:
            1. 点击「选择音乐 App 并开始」，选择正在播放的音乐 App。
            2. 取消选择会停止启动；系统停止投送时同步服务也会停止。
            3. 板子上电后会自动连接，开始播放即可同步歌曲与歌词。

            关于歌词:小米音乐息屏后通知可能冻结，本 App 会改从持续更新的
            MediaSession 读取当前歌曲与逐句歌词，无需额外设置。
            其它播放器需要自行打开「状态栏歌词」开关。

            歌名尚未到达时仍显示实时歌词；通知监听恢复后会自动补齐歌名。
        """.trimIndent()
    }
}
