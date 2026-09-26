package com.cyd.music

/**
 * MediaSession 与播放器通知的职责边界。
 *
 * 活动 MediaSession 是歌曲身份的主来源；小米会话只有歌词时由通知补歌名。
 * 小米音乐带 MIUI focus 字段的通知在息屏后会冻结，但 MediaSession 仍会
 * 持续更新歌词，所以此时连通知歌词也不能采用，否则重绑监听器会把旧歌词
 * 重新覆盖到板子上。
 */
object MediaSourcePolicy {

    fun notificationMaySetTrack(
        sessionPackage: String?, sessionHasTrack: Boolean = true
    ): Boolean = sessionPackage == null ||
        (sessionPackage == "com.miui.player" && !sessionHasTrack)

    fun notificationMaySetLyric(
        notificationPackage: String,
        sessionPackage: String?,
        hasMiuiFocus: Boolean
    ): Boolean {
        if (sessionPackage != null && notificationPackage != sessionPackage) return false
        // Xiaomi session titles keep updating while notification lyrics can
        // freeze on the lock screen, even without the optional focus JSON.
        if (sessionPackage == "com.miui.player") return false
        if (sessionPackage == notificationPackage && hasMiuiFocus) return false
        return true
    }
}
