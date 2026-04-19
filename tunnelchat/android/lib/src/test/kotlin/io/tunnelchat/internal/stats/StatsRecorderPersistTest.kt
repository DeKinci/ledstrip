package io.tunnelchat.internal.stats

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.internal.archive.StatsStore
import io.tunnelchat.internal.archive.TunnelchatDatabase
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class StatsRecorderPersistTest {
    private lateinit var db: TunnelchatDatabase
    private lateinit var store: StatsStore

    @Before
    fun setUp() {
        db = TunnelchatDatabase.inMemory(ApplicationProvider.getApplicationContext())
        store = StatsStore(db.stats())
    }

    @After
    fun tearDown() = db.close()

    @Test
    fun flush_thenLoadFrom_restoresCountersButNotRtt() = runTest {
        val a = StatsRecorder()
        a.incBleFramesIn(11); a.incProtoOut(22); a.incEchoProbesSent(3)
        a.recordEchoRtt(500)
        a.flush(store)

        val b = StatsRecorder()
        b.loadFrom(store)
        val s = b.snapshot()
        assertEquals(11L, s.bleFramesIn)
        assertEquals(22L, s.protoOut)
        assertEquals(3L, s.echoProbesSent)
        assertNull("RTT histogram must not round-trip", s.echoRttMsP50)
        assertNull(s.echoRttMsP95)
    }

    @Test
    fun loadFrom_emptyStoreYieldsZeros() = runTest {
        val r = StatsRecorder()
        r.loadFrom(store)
        assertEquals(0L, r.snapshot().bleFramesIn)
    }
}
