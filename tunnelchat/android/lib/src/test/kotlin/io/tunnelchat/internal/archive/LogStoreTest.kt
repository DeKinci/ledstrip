package io.tunnelchat.internal.archive

import androidx.test.core.app.ApplicationProvider
import io.tunnelchat.api.LogEntry
import io.tunnelchat.api.LogLevel
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
class LogStoreTest {
    private lateinit var db: TunnelchatDatabase

    @Before
    fun setUp() {
        db = TunnelchatDatabase.inMemory(ApplicationProvider.getApplicationContext())
    }

    @After
    fun tearDown() = db.close()

    private fun entry(i: Int) = LogEntry(
        tsMs = i.toLong(),
        level = LogLevel.Info,
        tag = "t",
        message = "m$i",
    )

    @Test
    fun append_thenSnapshot_returnsAll() = runTest {
        val store = LogStore(db.logs(), maxEntries = 100)
        repeat(10) { store.append(entry(it)) }
        val snap = store.snapshot()
        assertEquals(10, snap.size)
        assertEquals("m0", snap.first().message)
        assertEquals("m9", snap.last().message)
    }

    @Test
    fun rollover_dropsOldestPastCap() = runTest {
        val store = LogStore(db.logs(), maxEntries = 5)
        repeat(12) { store.append(entry(it)) }
        val snap = store.snapshot()
        assertEquals(5, snap.size)
        assertEquals(listOf("m7", "m8", "m9", "m10", "m11"), snap.map { it.message })
    }

    @Test
    fun clear_emptiesBuffer() = runTest {
        val store = LogStore(db.logs(), maxEntries = 100)
        repeat(3) { store.append(entry(it)) }
        store.clear()
        assertEquals(0, store.count())
    }
}
