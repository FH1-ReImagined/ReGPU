from pathlib import Path
import unittest

SOURCE = Path('src/gpu/renderer.cpp').read_text()

def resolve_band(candidates, serial):
    # Model the selection expressions asserted against the actual source below.
    last = next((item for item in candidates if item[1] == serial), None)
    return candidates[0] if len(candidates) == 1 else last

class ResolveSerialIntegration(unittest.TestCase):
    def test_source_contract(self):
        blit = SOURCE[SOURCE.index('bool BlitResolve('):SOURCE.index('// EDRAM resolve:')]
        stamp = 'dest.serial = ++g_resolveSerial;'
        self.assertEqual(blit.count(stamp), 1)
        self.assertGreater(blit.index(stamp), blit.index('C().list->DrawInstanced(3, 1, 0, 0);'))
        self.assertLess(blit.index('if (sr.right <= sr.left'), blit.index(stamp))
        conversion = SOURCE[SOURCE.index('bool ConvertExplicitScene('):SOURCE.index('// The scene is complete', SOURCE.index('bool ConvertExplicitScene('))]
        self.assertNotIn('g_resolveSerial', conversion)
        self.assertNotIn('d.serial', conversion)
        self.assertLess(conversion.index('g_explicitBlit.Record'), conversion.index('d.resource=f.explicitTargets'))
        self.assertIn('pitch != 0', SOURCE)
        self.assertIn('if (image.serial == g_resolveSerial)', SOURCE)
        self.assertIn('candidates == 1 ? any : last', SOURCE)

    def test_conversion_does_not_steal_last_resolve(self):
        # A conversion of B is not a resolve: A remains the last resolved image.
        current_serial = 7
        images = [('A', 7), ('B', 6)]
        self.assertEqual(resolve_band(images, current_serial), ('A', 7))
        self.assertEqual(resolve_band(list(reversed(images)), current_serial), ('A', 7))
        self.assertIsNone(resolve_band(images, 8))
        self.assertEqual(resolve_band([('B', 6)], 7), ('B', 6))
        self.assertIsNone(resolve_band([], 7))
        # A subsequent recorded B resolve advances both values and legitimately wins.
        self.assertEqual(resolve_band([('A', 7), ('B', 8)], 8), ('B', 8))

if __name__ == '__main__':
    unittest.main()
