package com.cyd.music

import android.annotation.SuppressLint
import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.ParcelUuid
import android.util.Log
import java.util.ArrayDeque
import java.util.UUID

/**
 * BLE 客户端:扫描并连接 CYD-Music 开发板(Nordic UART 透传服务)。
 *
 * 相比旧版修掉的几件事:
 *
 * 1. **GATT 泄漏**:旧代码在 `onScanResult` 里调 `stopScan()` 后立刻 `connectGatt`,
 *    但 stopScan 是异步的,回调还会再进来几次,于是建出多个 GATT 客户端且
 *    返回值被丢弃(拿不到句柄也就关不掉)。系统总共只有 ~32 个槽位,
 *    用完之后必须杀进程才能恢复。这里加 [connecting] 闸门并保存句柄。
 *
 * 2. **写操作互相踩**:旧代码用 `postDelayed(delay)` 排队,而 `delay` 每次调用
 *    都从 0 重新算,所以频谱帧和歌词会被排到同一个时隙。现在改成
 *    「等 onCharacteristicWrite 回来再发下一条」的真流控。
 *
 * 3. **协议行超过 MTU**:META 最坏情况 247 字节,板子 MTU 只有 180。
 *    按当前 MTU 切片发送,靠 '\n' 在板子那边重组。
 *
 * 4. **拼包用字符串**:旧代码先把字节转成 String 再找 '\n',一个汉字被
 *    切在两个包里就会解码出替换字符。现在在**字节**层面拼包。
 */
@SuppressLint("MissingPermission") // 所有入口都经 hasPermission 检查，异常路径也有 try/catch。
class BleClient(
    private val context: Context,
    private val onStatus: (String) -> Unit,
    private val onData: (String) -> Unit
) {
    companion object {
        const val NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
        const val NUS_WRITE = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
        const val NUS_NOTIFY = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
        private const val CCCD = "00002902-0000-1000-8000-00805f9b34fb"
        const val TARGET_NAME = "CYD-Music"
        private const val TAG = "BleClient"

        /** 控制指令队列上限:板子卡住时丢最老的,不让内存无限涨 */
        private const val CTRL_QUEUE_MAX = 32
        /** 重连间隔 */
        private const val RECONNECT_MS = 3000L
        /** 收包缓冲上限,防对端乱发不带换行的数据把内存吃光 */
        private const val RX_MAX = 4096
    }

    private val adapter: BluetoothAdapter? =
        context.getSystemService(BluetoothManager::class.java)?.adapter

    private var gatt: BluetoothGatt? = null
    private var nusChar: BluetoothGattCharacteristic? = null
    private var mtu = 23

    @Volatile private var connected = false
    @Volatile private var connecting = false
    @Volatile private var stopped = false
    @Volatile private var scanning = false

    /** 收包缓冲:字节层面拼,避免把多字节字符切碎 */
    private val rxBuffer = java.io.ByteArrayOutputStream()

    /** 可靠队列:META / LRC / PLAY,一条都不能丢 */
    private val ctrlQueue = ArrayDeque<ByteArray>()
    /** 只保留最新一帧的频谱槽:堵住时旧帧没有任何价值,直接覆盖 */
    private var pendingSpec: ByteArray? = null
    /** 播放状态是周期心跳，只保留最新一条，避免断线时堆满可靠队列。 */
    private var pendingPlay: ByteArray? = null
    /**
     * 正在发送中的那一条(可能要切成多片)。
     * 必须单独存:光记偏移量不行 —— SPEC 发到一半时来了 META,
     * 下一轮会取出 META 却带着 SPEC 的偏移,两条都发烂。
     */
    private var current: ByteArray? = null
    /** [current] 里已经发出去多少字节 */
    private var currentOffset = 0
    /** 有写操作在飞,等 onCharacteristicWrite 回来 */
    private var writeBusy = false

    private val writeThread = HandlerThread("bleWrite").apply { start() }
    private val writeHandler = Handler(writeThread.looper)

    // ==================== 扫描 ====================

    fun start() {
        stopped = false
        if (adapter == null || !adapter.isEnabled) {
            onStatus("手机蓝牙未开启")
            return
        }
        if (connected || connecting) return
        if (!hasPermission(scanPerm())) {
            onStatus("缺少蓝牙权限")
            return
        }
        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            onStatus("无法启动 BLE 扫描")
            return
        }
        if (scanning) return

        onStatus("正在扫描 $TARGET_NAME ...")
        val filters = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(NUS_SERVICE)).build(),
            ScanFilter.Builder().setDeviceName(TARGET_NAME).build()
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        try {
            scanner.startScan(filters, settings, scanCb)
            scanning = true
        } catch (e: Exception) {
            Log.w(TAG, "startScan", e)
            onStatus("扫描启动失败: ${e.message}")
        }
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            if (hasPermission(scanPerm())) {
                adapter?.bluetoothLeScanner?.stopScan(scanCb)
            }
        } catch (e: Exception) {
            Log.w(TAG, "stopScan", e)
        }
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            val dev = result?.device ?: return
            // 闸门:stopScan 是异步的,不加这个会连出多个 GATT 客户端(槽位泄漏)
            if (connecting || connected || stopped) return
            // 不用再核对名字/UUID:startScan 传了 ScanFilter,系统已经筛过
            connecting = true
            stopScan()
            connect(dev)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            onStatus("扫描失败,错误码 $errorCode")
            scheduleReconnect()
        }
    }

    // ==================== 连接 ====================

    private fun connect(dev: BluetoothDevice) {
        if (!hasPermission(connectPerm())) {
            onStatus("缺少蓝牙连接权限")
            connecting = false
            return
        }
        onStatus("正在连接 ${dev.address} ...")
        try {
            // 保存返回值:旧代码丢掉句柄,连不上时既关不掉也重连不了
            gatt = dev.connectGatt(context, false, gattCb, BluetoothDevice.TRANSPORT_LE)
            if (gatt == null) {
                connecting = false
                onStatus("connectGatt 返回 null")
                scheduleReconnect()
            }
        } catch (e: Exception) {
            connecting = false
            Log.w(TAG, "connectGatt", e)
            scheduleReconnect()
        }
    }

    fun stop() {
        stopped = true
        stopScan()
        writeHandler.removeCallbacksAndMessages(null)
        closeGatt()
        clearQueues()
    }

    /** 断线时清空:队列里的东西对新连接没意义,而且会顶着旧偏移发出去 */
    private fun clearQueues() {
        synchronized(ctrlQueue) {
            ctrlQueue.clear()
            pendingSpec = null
            pendingPlay = null
            current = null
            currentOffset = 0
            writeBusy = false
        }
    }

    private fun closeGatt() {
        connected = false
        connecting = false
        nusChar = null
        mtu = 23
        rxBuffer.reset()
        val g = gatt ?: return
        gatt = null
        try {
            g.disconnect()
        } catch (_: Exception) {
        }
        try {
            g.close()      // 不 close 就会一直占着系统的 GATT 槽位
        } catch (_: Exception) {
        }
    }

    private fun scheduleReconnect() {
        if (stopped) return
        writeHandler.postDelayed({
            if (!stopped && !connected && !connecting) {
                closeGatt()
                start()
            }
        }, RECONNECT_MS)
    }

    // ==================== GATT 回调 ====================

    private val gattCb = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    onStatus("已连接,正在发现服务 ...")
                    try {
                        // 先提到高优先级,发现服务和后面的写都会快一截
                        g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                        g.discoverServices()
                    } catch (e: Exception) {
                        Log.w(TAG, "discoverServices", e)
                    }
                }

                BluetoothProfile.STATE_DISCONNECTED -> {
                    val wasConnected = connected
                    closeGatt()
                    clearQueues()
                    if (!stopped) {
                        onStatus(
                            if (wasConnected) "连接断开,${RECONNECT_MS / 1000}秒后重连"
                            else "连接失败(status=$status),重试中"
                        )
                        scheduleReconnect()
                    }
                }
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                onStatus("服务发现失败 $status")
                closeGatt(); scheduleReconnect(); return
            }
            val svc = g.getService(UUID.fromString(NUS_SERVICE))
            if (svc == null) {
                onStatus("板子上没有 NUS 服务")
                closeGatt(); scheduleReconnect(); return
            }
            nusChar = svc.getCharacteristic(UUID.fromString(NUS_WRITE))
            if (nusChar == null) {
                onStatus("找不到写特征")
                closeGatt(); scheduleReconnect(); return
            }
            // 先订阅通知,等 onDescriptorWrite 回来再谈 MTU。
            // 旧代码同时发 writeDescriptor 和 requestMtu,两个操作抢一条 GATT 队列,
            // 后发的那个会直接被丢掉。
            val notify = svc.getCharacteristic(UUID.fromString(NUS_NOTIFY))
            if (notify == null) {
                onStatus("找不到通知特征,按钮将不可用")
                afterSetup(g)
                return
            }
            try {
                g.setCharacteristicNotification(notify, true)
                val cccd = notify.getDescriptor(UUID.fromString(CCCD))
                if (cccd == null) {
                    afterSetup(g)
                } else {
                    writeCccd(g, cccd)
                }
            } catch (e: Exception) {
                Log.w(TAG, "订阅通知失败", e)
                afterSetup(g)
            }
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int
        ) {
            afterSetup(g)
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = newMtu
            markReady()
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int
        ) {
            // 真流控:上一条落地了才发下一条
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "写失败 status=$status")
            }
            onWriteComplete(status == BluetoothGatt.GATT_SUCCESS)
        }

        // Android 13+ 的新签名
        override fun onCharacteristicChanged(
            g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray
        ) {
            feedRx(value)
        }

        // Android 13 以下只会回调这个旧签名,所以必须留着。
        // 自己也标上 @Deprecated,否则编译器会警告「覆盖了废弃成员但没标废弃」。
        @Deprecated("系统在 API 33 用带 value 参数的新签名替代了它,这里为兼容旧机器保留")
        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            g: BluetoothGatt, c: BluetoothGattCharacteristic
        ) {
            if (Build.VERSION.SDK_INT < 33) feedRx(c.value ?: return)
        }
    }

    private fun writeCccd(g: BluetoothGatt, cccd: BluetoothGattDescriptor) {
        val on = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeDescriptor(cccd, on)
            } else {
                @Suppress("DEPRECATION")
                run { cccd.value = on; g.writeDescriptor(cccd) }
            }
        } catch (e: Exception) {
            Log.w(TAG, "writeDescriptor", e)
            afterSetup(g)
        }
    }

    /** 订阅完成后再谈 MTU;谈不动也照样能用,只是要多切几片 */
    private fun afterSetup(g: BluetoothGatt) {
        try {
            if (!g.requestMtu(247)) markReady()
        } catch (e: Exception) {
            Log.w(TAG, "requestMtu", e)
            markReady()
        }
    }

    private fun markReady() {
        if (connected) return
        connected = true
        connecting = false
        onStatus("板子已就绪(MTU $mtu)")
        // 重连后把当前状态整套补发,否则板子会一直空着等到下次切歌
        NowPlaying.resend()
        // 统一在 writeHandler 上发,别在 binder 回调线程里写 GATT
        writeHandler.post { pump() }
    }

    // ==================== 发送队列 ====================

    /**
     * 写一行协议文本。
     * SPEC 走「只留最新一帧」的槽,其余走可靠队列 —— 频谱帧堵住时丢掉旧帧
     * 没有损失,但歌名和歌词丢一条板子上就一直是错的。
     */
    fun writeLine(line: String) {
        if (stopped) return
        val bytes = (line + "\n").toByteArray(Charsets.UTF_8)
        synchronized(ctrlQueue) {
            if (line.startsWith("SPEC|")) {
                pendingSpec = bytes
            } else if (line.startsWith("PLAY|")) {
                pendingPlay = bytes
            } else {
                if (ctrlQueue.size >= CTRL_QUEUE_MAX) ctrlQueue.pollFirst()
                ctrlQueue.addLast(bytes)
            }
        }
        writeHandler.post { pump() }
    }

    /** 取出下一片发出去。必须在 writeHandler 上跑,保证串行。 */
    private fun pump() {
        if (!connected || stopped) return
        val g = gatt ?: return
        val c = nusChar ?: return

        val chunk: ByteArray
        val sentHead: ByteArray
        val sentOffset: Int
        synchronized(ctrlQueue) {
            if (writeBusy) return
            // 上一条还没发完就接着发它;发完了再取新的。
            // 控制指令优先于频谱:歌词比波形重要。
            if (current == null) {
                current = ctrlQueue.pollFirst()
                    ?: pendingPlay?.also { pendingPlay = null }
                    ?: pendingSpec?.also { pendingSpec = null }
                currentOffset = 0
            }
            val head = current ?: return

            // 按当前 MTU 切片。ATT 头占 3 字节,剩下的才是载荷。
            // 板子靠 '\n' 重组,所以从中间切开是安全的。
            val payload = (mtu - 3).coerceAtLeast(20)
            val remain = head.size - currentOffset
            val take = if (remain > payload) payload else remain
            chunk = head.copyOfRange(currentOffset, currentOffset + take)
            sentHead = head
            sentOffset = currentOffset
            currentOffset += take
            if (currentOffset >= head.size) {
                current = null
                currentOffset = 0
            }
            writeBusy = true
        }

        // 必须用「带响应」的写:固件那边特征只声明了 NIMBLE_PROPERTY::WRITE,
        // 没有 WRITE_NR,写无响应会被拒。顺带也正好用它做流控。
        val ok = try {
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(
                    c, chunk, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                ) == android.bluetooth.BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                run {
                    c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    c.value = chunk
                    g.writeCharacteristic(c)
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "writeCharacteristic", e)
            false
        }

        if (!ok) {
            // 底层队列满了。把刚才那一片**退回去**再重试 ——
            // 偏移已经推进过了,不回滚这一片就凭空丢了。
            synchronized(ctrlQueue) {
                current = sentHead
                currentOffset = sentOffset     // 回到这一片发之前的位置
                writeBusy = false
            }
            // 不能就地重试,会递归爆栈
            writeHandler.postDelayed({ pump() }, 20)
        }
    }

    private fun onWriteComplete(@Suppress("UNUSED_PARAMETER") ok: Boolean) {
        synchronized(ctrlQueue) { writeBusy = false }
        writeHandler.post { pump() }
    }

    // ==================== 收包 ====================

    /**
     * 在字节层面按 '\n' 拆行。
     * 旧代码先 `String(bytes)` 再找换行,一个汉字被切在两个通知包里就会
     * 解码成替换字符,再也拼不回来。
     */
    private fun feedRx(data: ByteArray) {
        synchronized(rxBuffer) {
            if (rxBuffer.size() + data.size > RX_MAX) rxBuffer.reset()
            rxBuffer.write(data)
            val all = rxBuffer.toByteArray()
            var start = 0
            var i = 0
            while (i < all.size) {
                if (all[i] == '\n'.code.toByte()) {
                    val line = String(all, start, i - start, Charsets.UTF_8)
                        .trim { it <= ' ' }
                    if (line.isNotEmpty()) {
                        Log.d(TAG, "板端上行: $line")
                        onData(line)
                    }
                    start = i + 1
                }
                i++
            }
            rxBuffer.reset()
            if (start < all.size) rxBuffer.write(all, start, all.size - start)
        }
    }

    // ==================== 权限 ====================

    private fun scanPerm() =
        if (Build.VERSION.SDK_INT >= 31) Manifest.permission.BLUETOOTH_SCAN
        else Manifest.permission.ACCESS_FINE_LOCATION

    private fun connectPerm() =
        if (Build.VERSION.SDK_INT >= 31) Manifest.permission.BLUETOOTH_CONNECT
        else Manifest.permission.BLUETOOTH

    private fun hasPermission(p: String) =
        context.checkSelfPermission(p) == PackageManager.PERMISSION_GRANTED
}
