import unittest
from nebula_frame_telemetry import MAGIC, PACKET, Rates, decode_packet


def packet(seq=1, time=1000, copies=20, first=10, pid=123, creation=456):
    values = [0] * 32
    values[:13] = [MAGIC, 1, PACKET.size, pid, creation, seq, time, 1000, 1, copies, first, 30, 20]
    return tuple(values)


class TelemetryTests(unittest.TestCase):
    def test_reject_stale_inactive_wrong_process_and_truncated(self):
        p = packet()
        raw = PACKET.pack(*p)
        self.assertEqual(decode_packet(raw, 123, 456, 1500), p)
        for pid, birth, now in [(124, 456, 1500), (123, 457, 1500),
                                (123, 456, 999), (123, 456, 3501)]:
            self.assertIsNone(decode_packet(raw, pid, birth, now))
        self.assertIsNone(decode_packet(raw[:-1], 123, 456, 1500))
        p = list(p); p[8] = 0
        self.assertIsNone(decode_packet(PACKET.pack(*p), 123, 456, 1500))

    def test_copies_and_first_returns_remain_separate(self):
        r = Rates()
        self.assertIsNone(r.accept(packet()))
        result = r.accept(packet(2, 2000, 60, 40))
        self.assertEqual(result['copy_hz'], 40)
        self.assertEqual(result['first_return_hz'], 30)
        self.assertEqual(result['repeat_hz'], 0)
        self.assertEqual(r.accept(packet(2, 2000, 60, 40)), result)
        self.assertIsNone(r.accept(packet(3, 6000, 70, 45)))

    def test_pid_reuse_and_counter_reset_do_not_make_spikes(self):
        r = Rates(); r.accept(packet())
        self.assertIsNone(r.accept(packet(2, 2000, 60, 40, creation=789)))
        self.assertIsNone(r.accept(packet(3, 3000, 1, 1, creation=789)))


if __name__ == '__main__':
    unittest.main()
