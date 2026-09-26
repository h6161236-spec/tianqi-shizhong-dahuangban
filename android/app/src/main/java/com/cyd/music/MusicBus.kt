package com.cyd.music

/**
 * 进程内消息总线。
 * 同一个 App 进程里,前台服务(蓝牙/频谱)、通知监听(歌词)、
 * 主界面(状态显示)通过这个单例互相通信,避免复杂的绑定。
 */
object MusicBus {
    /** 服务是否在运行(通知监听用它判断要不要转发数据) */
    @Volatile var serviceRunning: Boolean = false

    /** 通知监听是否已连上(比查授权列表准:授权了但没连上一样收不到通知) */
    @Volatile var notificationAccess: Boolean = false

    /** 通知监听连接状态变化；服务用它重新挂载 MediaSession 并补发当前通知。 */
    @Volatile var notificationListenerChanged: ((Boolean) -> Unit)? = null

    /**
     * MediaWatcher 当前挂着的播放器包名,没挂上时为 null。
     *
     * 通知监听用它决定「歌曲身份」该不该由自己来定:MediaSession 的
     * 元数据是结构化字段,比从通知文本里猜可靠。两边都往 [NowPlaying]
     * 写 setTrack 且结论不一致时会来回打架,每次都重发 META 并清空歌词。
     */
    @Volatile var sessionPackage: String? = null
    /** Xiaomi may expose only a lyric, with no track identity in its session. */
    @Volatile var sessionHasTrack: Boolean = false

    /** 数据出口:服务启动时注册,把一行协议文本写入蓝牙 */
    @Volatile var sender: ((String) -> Unit)? = null

    /** 状态出口:显示到主界面 */
    @Volatile var statusReporter: ((String) -> Unit)? = null

    /** 板子按钮指令出口:由前台服务注册,转给 MediaWatcher 执行媒体控制 */
    @Volatile var mediaAction: ((String) -> Unit)? = null

    fun send(line: String) {
        sender?.invoke(line)
    }

    fun status(s: String) {
        statusReporter?.invoke(s)
    }
}
