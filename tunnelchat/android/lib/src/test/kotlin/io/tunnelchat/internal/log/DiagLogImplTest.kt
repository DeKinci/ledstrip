package io.tunnelchat.internal.log

import io.tunnelchat.api.LogEntry
import io.tunnelchat.api.LogLevel
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class DiagLogImplTest {

    private fun entry(level: LogLevel, i: Int = 0): LogEntry =
        LogEntry(tsMs = i.toLong(), level = level, tag = "T", message = "m$i")

    private class FakePersistor : DiagLogImpl.Persistor {
        val appended = mutableListOf<LogEntry>()
        var cleared = 0
        override suspend fun append(entry: LogEntry) { appended += entry }
        override suspend fun clear() { cleared++ }
    }

    @Test
    fun append_belowMinLevel_dropped() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        log.setMinLevel(LogLevel.Warn)
        log.append(entry(LogLevel.Info))
        log.append(entry(LogLevel.Debug))
        assertTrue(log.snapshot().isEmpty())
    }

    @Test
    fun append_atOrAboveMinLevel_kept() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        log.setMinLevel(LogLevel.Warn)
        log.append(entry(LogLevel.Warn, 1))
        log.append(entry(LogLevel.Error, 2))
        assertEquals(listOf(1L, 2L), log.snapshot().map { it.tsMs })
    }

    @Test
    fun snapshot_isStable_laterAppendsDoNotMutateIt() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        log.append(entry(LogLevel.Info, 1))
        val snap = log.snapshot()
        log.append(entry(LogLevel.Info, 2))
        log.append(entry(LogLevel.Info, 3))
        assertEquals(1, snap.size)
        assertEquals(1L, snap[0].tsMs)
    }

    @Test
    fun ring_rollsOverPastMax_newestKept() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)), maxInMemory = 3)
        repeat(5) { log.append(entry(LogLevel.Info, it)) }
        val snap = log.snapshot()
        assertEquals(3, snap.size)
        assertEquals(listOf(2L, 3L, 4L), snap.map { it.tsMs })
    }

    @Test
    fun clear_emptiesBufferAndCascadesToPersistor() = runTest {
        val scheduler = testScheduler
        val scope = TestScope(StandardTestDispatcher(scheduler))
        val p = FakePersistor()
        val log = DiagLogImpl(scope, persistor = p)
        log.append(entry(LogLevel.Info, 1))
        log.append(entry(LogLevel.Info, 2))
        log.clear()
        assertTrue(log.snapshot().isEmpty())
        scope.advanceUntilIdle()
        assertEquals(1, p.cleared)
    }

    @Test
    fun persistor_receivesAppends() = runTest {
        val scope = TestScope(StandardTestDispatcher(testScheduler))
        val p = FakePersistor()
        val log = DiagLogImpl(scope, persistor = p)
        log.append(entry(LogLevel.Info, 1))
        log.append(entry(LogLevel.Warn, 2))
        scope.advanceUntilIdle()
        assertEquals(listOf(1L, 2L), p.appended.map { it.tsMs })
    }

    @Test
    fun persistor_doesNotReceiveFilteredEntries() = runTest {
        val scope = TestScope(StandardTestDispatcher(testScheduler))
        val p = FakePersistor()
        val log = DiagLogImpl(scope, persistor = p)
        log.setMinLevel(LogLevel.Error)
        log.append(entry(LogLevel.Info, 1))
        scope.advanceUntilIdle()
        assertTrue(p.appended.isEmpty())
    }

    @Test
    fun entries_streamsAppends() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        val collected = mutableListOf<LogEntry>()
        val job = launch { log.entries.take(3).toList(collected) }
        // Give the subscriber a chance to subscribe before emitting.
        advanceUntilIdle()
        log.append(entry(LogLevel.Info, 1))
        log.append(entry(LogLevel.Info, 2))
        log.append(entry(LogLevel.Info, 3))
        job.join()
        assertEquals(listOf(1L, 2L, 3L), collected.map { it.tsMs })
    }

    @Test
    fun setMinLevel_takesEffectImmediately() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        log.append(entry(LogLevel.Debug, 1))
        log.setMinLevel(LogLevel.Error)
        log.append(entry(LogLevel.Info, 2))
        log.append(entry(LogLevel.Error, 3))
        assertEquals(listOf(1L, 3L), log.snapshot().map { it.tsMs })
    }

    @Test
    fun defaultMinLevel_isVerbose() = runTest {
        val log = DiagLogImpl(TestScope(StandardTestDispatcher(testScheduler)))
        log.append(entry(LogLevel.Verbose, 1))
        assertFalse(log.snapshot().isEmpty())
    }
}
