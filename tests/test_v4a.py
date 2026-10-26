"""Synthetic tests only: these values are NOT measured MCU performance."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from parse_v4a_audio import EXPECTED, evaluate


def fixture(platform="ESP32-S3-I2S", change=None):
    ac = platform == "AC7911B8-Felucca-ALNK0"
    clock = 24000000 if ac else 240000000
    deadline = 69632 if ac else 696599
    header = (f"FM6-V4A platform={platform} clock_hz={clock} cpu_hz=0 "
              "dsp_nominal_sr=44100 ctl=32 quantum_frames=128")
    if ac:
        header += " transport_sr_num=24000000 transport_sr_div=544"
    lines = [header]
    for v in EXPECTED:
        scale = 10 if ac else 100
        row = dict(voices=v, quanta=256, samples=32768, deadline_ticks=deadline,
                   dsp_avg=v*scale, dsp_p95=v*scale+10, dsp_p99=v*scale+20,
                   dsp_max=v*scale+30, dsp_miss=0, write_avg=5, write_p99=6,
                   write_max=7, total_avg=v*scale+5, total_p99=v*scale+26,
                   total_max=v*scale+37, total_miss=0, starve=0, write_fail=0,
                   dma_sent=256, send_q_ovf=0, warm_errors=0, sink="0123abcd")
        if change:
            change(v, row)
        lines.append("FM6-V4A " + " ".join(f"{k}={x}" for k,x in row.items()))
    return "\n".join(lines + ["FM6-V4A DONE", ""])


class ParserTests(unittest.TestCase):
    def test_s3_complete(self):
        r = evaluate(fixture(), source="fixture")
        self.assertEqual(r["safe_max_tested_prefix"], 24)
        self.assertFalse(r["product_polyphony_guaranteed"])
        self.assertEqual(r["source"], "fixture")

    def test_ac_complete(self):
        self.assertEqual(evaluate(fixture("AC7911B8-Felucca-ALNK0"))["safe_max_tested_prefix"],24)

    def test_starvation_fails(self):
        r = evaluate(fixture(change=lambda v,r: r.update(starve=1) if v==24 else None))
        self.assertEqual(r["safe_max_tested_prefix"],20)

    def test_transport_errors_fail(self):
        for key in ("write_fail", "send_q_ovf", "warm_errors"):
            with self.subTest(key=key):
                r=evaluate(fixture(change=lambda v,r: r.update({key:1}) if v==24 else None))
                self.assertEqual(r["safe_max_tested_prefix"],20)

    def test_no_dma_cannot_pass(self):
        r=evaluate(fixture(change=lambda v,r:r.update(dma_sent=0)))
        self.assertEqual(r["safe_max_tested_prefix"],0)

    def test_dma_snapshot_boundary(self):
        for count in (254,255,256,257,258):
            with self.subTest(count=count):
                self.assertEqual(evaluate(fixture(change=lambda v,r:r.update(dma_sent=count)))["safe_max_tested_prefix"],24)

    def test_dma_counts_out_of_window(self):
        for count in (253,259):
            self.assertEqual(evaluate(fixture(change=lambda v,r:r.update(dma_sent=count)))["safe_max_tested_prefix"],0)

    def test_nonmonotonic_safe_does_not_skip_failed_tier(self):
        r=evaluate(fixture(change=lambda v,r:r.update(starve=1) if v==12 else None))
        self.assertTrue(r["rows"][-1]["safe"])
        self.assertEqual(r["safe_max_tested_prefix"],8)

    def test_s3_blocking_wait_not_counted_as_dsp(self):
        r=evaluate(fixture(change=lambda v,r:r.update(total_max=800000,total_miss=1)))
        self.assertEqual(r["safe_max_tested_prefix"],24)

    def test_ac_total_miss_fails_even_when_half_flag_aliases(self):
        r=evaluate(fixture("AC7911B8-Felucca-ALNK0",change=lambda v,r:r.update(total_max=80000,total_miss=1)))
        self.assertEqual(r["safe_max_tested_prefix"],0)

    def test_dsp_miss_fails(self):
        r=evaluate(fixture(change=lambda v,r:r.update(dsp_max=800000,dsp_miss=1)))
        self.assertEqual(r["safe_max_tested_prefix"],0)

    def test_p99_gate(self):
        r=evaluate(fixture(change=lambda v,r:r.update(dsp_p99=600000,dsp_max=610000)))
        self.assertEqual(r["safe_max_tested_prefix"],0)

    def test_invalid_threshold(self):
        for t in (0,-1,100,101,math.inf,math.nan):
            with self.subTest(t=t),self.assertRaises(ValueError): evaluate(fixture(),t)

    def test_zero_deadline(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(deadline_ticks=0)))

    def test_bad_deadline_ratio(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(deadline_ticks=174150)))

    def test_negative_stats(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(dsp_p99=-1)))

    def test_invalid_percentiles(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(dsp_p95=100000)))

    def test_bad_miss_count(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(dsp_max=800000)))

    def test_bad_sample_count(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(samples=0)))

    def test_short_sample_window(self):
        with self.assertRaises(ValueError): evaluate(fixture(change=lambda v,r:r.update(quanta=10,samples=1280)))

    def test_missing_done(self):
        with self.assertRaises(ValueError): evaluate(fixture().replace("FM6-V4A DONE", ""))

    def test_missing_voice(self):
        with self.assertRaises(ValueError): evaluate("\n".join(l for l in fixture().splitlines() if not l.startswith("FM6-V4A voices=12 ")))

    def test_duplicate_voice(self):
        text=fixture(); row=text.splitlines()[1]
        with self.assertRaises(ValueError): evaluate(text.replace(row,row+"\n"+row))

    def test_two_runs(self):
        with self.assertRaises(ValueError): evaluate(fixture()+fixture())

    def test_duplicate_field(self):
        with self.assertRaises(ValueError): evaluate(fixture().replace("voices=1 ","voices=1 voices=1 "))

    def test_premature_done(self):
        with self.assertRaises(ValueError): evaluate(fixture().replace("FM6-V4A voices=1 ","FM6-V4A DONE\nFM6-V4A voices=1 "))

    def test_device_fail(self):
        with self.assertRaises(ValueError): evaluate(fixture()+"FM6-V4A FAIL timeout\n")

    def test_fixture_cannot_be_declared_physical(self):
        with self.assertRaises(ValueError): evaluate(fixture("fixture"), source="physical")

    def test_label_not_provenance(self):
        self.assertEqual(evaluate(fixture())["source"],"unspecified")

    def test_legacy_s3_tick_alias(self):
        self.assertEqual(evaluate(fixture().replace("deadline_ticks=","deadline_cycles="))["safe_max_tested_prefix"],24)

    def test_ansi_crlf(self):
        text=fixture().replace("FM6-V4A", "\x1b[32mFM6-V4A").replace("\n", "\x1b[0m\r\n")
        self.assertEqual(evaluate(text)["safe_max_tested_prefix"],24)


if __name__=="__main__": unittest.main()
