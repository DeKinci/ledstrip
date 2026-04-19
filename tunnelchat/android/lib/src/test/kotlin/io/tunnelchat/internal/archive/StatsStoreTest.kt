package io.tunnelchat.internal.archive

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.api.Statistics
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class StatsStoreTest {
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
    fun load_returnsZerosWhenEmpty() = runTest {
        assertEquals(Statistics(), store.load())
    }

    @Test
    fun flush_thenLoad_roundTrips() = runTest {
        val s = Statistics(
            bleFramesIn = 10, bleFramesOut = 20, bleReconnects = 3,
            textMessagesIn = 5, textMessagesOut = 8,
            blobChunksIn = 100, blobChunksOut = 200, blobCrcRejects = 7,
            blobPartialGcDrops = 2, protoIn = 4, protoOut = 4,
            protoUnknownSchema = 1, echoProbesSent = 9,
        )
        store.flush(s)
        assertEquals(s.copy(echoRttMsP50 = null, echoRttMsP95 = null), store.load())
    }

    @Test
    fun flush_singleRowOverwrite() = runTest {
        store.flush(Statistics(bleFramesIn = 1))
        store.flush(Statistics(bleFramesIn = 2))
        assertEquals(2L, store.load().bleFramesIn)
    }
}
