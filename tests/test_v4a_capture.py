"""Mock serial tests: no board is accessed."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from run_v4a_hardware import capture

class Serial:
    def __init__(self,chunks): self.chunks=iter(chunks); self.closed=False; self.cleared=False; self.writes=[]
    def readline(self):
        value=next(self.chunks,b'')
        if isinstance(value,Exception): raise value
        return value
    def reset_input_buffer(self): self.cleared=True
    def write(self,b): self.writes.append(b)
    def flush(self): pass
    def close(self): self.closed=True

class CaptureTests(unittest.TestCase):
    def run_capture(self,path,ser,trigger=None):
        with patch('run_v4a_hardware.wait_open',return_value=ser):
            return capture(port='MOCK',baud=115200,timeout=.1,open_timeout=.1,log_path=path,trigger=trigger)
    def test_fragmented_lines_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x.log'; ser=Serial([b'FM6-V4A DO',b'NE\r\n'])
            self.assertEqual(self.run_capture(p,ser),'FM6-V4A DONE\r\n')
            self.assertEqual(p.read_bytes(),b'FM6-V4A DONE\r\n')
            self.assertFalse(ser.cleared); self.assertTrue(ser.closed)
    def test_disconnect_keeps_partial(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x.log'; ser=Serial([b'partial\n',OSError('disconnected')])
            with self.assertRaises(OSError): self.run_capture(p,ser)
            self.assertEqual(p.read_bytes(),b'partial\n'); self.assertTrue(ser.closed)
    def test_fm1_trigger(self):
        with tempfile.TemporaryDirectory() as d:
            ser=Serial([b'FM6-V4A DONE\n']); self.run_capture(Path(d)/'x',ser,b'fm6audiobench\r\n')
            self.assertTrue(ser.cleared); self.assertEqual(ser.writes,[b'fm6audiobench\r\n'])
    def test_existing_log_not_overwritten(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x'; p.write_bytes(b'old')
            with self.assertRaises(FileExistsError): self.run_capture(p,Serial([]))
            self.assertEqual(p.read_bytes(),b'old')
    def test_timeout_preserves_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x'; self.run_capture(p,Serial([b'incomplete']))
            self.assertEqual(p.read_bytes(),b'incomplete')

if __name__=='__main__': unittest.main()
