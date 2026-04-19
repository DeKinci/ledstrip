package io.tunnelchat.internal.stats

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Test

class StatsRecorderTest {

    @Test
    fun increment_updatesCounterAndState() {
        val r = StatsRecorder()
        r.incBleFramesIn()
        r.incBleFramesIn(4)
        assertEquals(5L, r.snapshot().bleFramesIn)
        assertEquals(5L, r.state.value.bleFramesIn)
    }

    @Test
    fun allCounters_incrementIndependently() {
        val r = StatsRecorder()
        r.incBleFramesIn(1); r.incBleFramesOut(2); r.incBleReconnects(3)
        r.incTextMessagesIn(4); r.incTextMessagesOut(5)
        r.incBlobChunksIn(6); r.incBlobChunksOut(7); r.incBlobCrcRejects(8); r.incBlobPartialGcDrops(9)
        r.incProtoIn(10); r.incProtoOut(11); r.incProtoUnknownSchema(12)
        r.incEchoProbesSent(13)

        val s = r.snapshot()
        assertEquals(1L, s.bleFramesIn)
        assertEquals(2L, s.bleFramesOut)
        assertEquals(3L, s.bleReconnects)
        assertEquals(4L, s.textMessagesIn)
        assertEquals(5L, s.textMessagesOut)
        assertEquals(6L, s.blobChunksIn)
        assertEquals(7L, s.blobChunksOut)
        assertEquals(8L, s.blobCrcRejects)
        assertEquals(9L, s.blobPartialGcDrops)
        assertEquals(10L, s.protoIn)
        assertEquals(11L, s.protoOut)
        assertEquals(12L, s.protoUnknownSchema)
        assertEquals(13L, s.echoProbesSent)
    }

    @Test
    fun reset_zerosAllCountersAndRtt() {
        val r = StatsRecorder()
        r.incBleFramesIn(99)
        r.recordEchoRtt(50)
        r.reset()
        val s = r.snapshot()
        assertEquals(0L, s.bleFramesIn)
        assertNull(s.echoRttMsP50)
        assertNull(s.echoRttMsP95)
    }

    @Test
    fun rtt_percentilesFromSamples() {
        val r = StatsRecorder()
        // 1..100 samples: p50 = 50 (ceil(0.5*100)-1 = 49 → value 50), p95 = 95.
        (1..100L).shuffled().forEach { r.recordEchoRtt(it) }
        val s = r.snapshot()
        assertEquals(50L, s.echoRttMsP50)
        assertEquals(95L, s.echoRttMsP95)
    }

    @Test
    fun rtt_ringDropsOldestPastCap() {
        val r = StatsRecorder(rttCap = 4)
        // 4 samples of 100, then 4 samples of 1 → ring holds only the recent 1s.
        repeat(4) { r.recordEchoRtt(100) }
        repeat(4) { r.recordEchoRtt(1) }
        val s = r.snapshot()
        assertEquals(1L, s.echoRttMsP50)
        assertEquals(1L, s.echoRttMsP95)
    }

    @Test
    fun rtt_singleSampleIsBothPercentiles() {
        val r = StatsRecorder()
        r.recordEchoRtt(42)
        val s = r.snapshot()
        assertEquals(42L, s.echoRttMsP50)
        assertEquals(42L, s.echoRttMsP95)
    }

    @Test
    fun snapshot_isImmutableCopy() {
        val r = StatsRecorder()
        r.incBleFramesIn()
        val first = r.snapshot()
        r.incBleFramesIn()
        val second = r.snapshot()
        assertEquals(1L, first.bleFramesIn)
        assertEquals(2L, second.bleFramesIn)
        assertNotSame(first, second)
    }

    @Test
    fun state_reflectsLatestSnapshotValue() {
        val r = StatsRecorder()
        assertEquals(0L, r.state.value.bleFramesIn)
        r.incBleFramesIn(7)
        assertEquals(7L, r.state.value.bleFramesIn)
    }
}
