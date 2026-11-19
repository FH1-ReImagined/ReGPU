from pathlib import Path
import unittest
class Checks(unittest.TestCase):
 def test_original_resolve_inverse(self):
  now=Path('src/gpu/renderer.cpp').read_text(); old=Path('../regpu-native-scene-dimensions/src/gpu/renderer.cpp').read_text();
  # Compare the function against the pinned clean base, not its trace-edited sibling.
  import subprocess
  old=subprocess.check_output(['git','show','HEAD:src/gpu/renderer.cpp'],text=True)
  start=lambda s:s.index('BlitResolve(bool depth,'); end=lambda s:s.index('// EDRAM resolve:',start(s))
  n=now[start(now):end(now)].replace('return false;','return;').replace('    return true; // the original resolve draw and state restoration were recorded\n','')
  a=n.index('    auto sizing=');b=n.index('    EnsureResolved(',a); original=old[old.index('    const uint32_t hostWidth =',start(old)):old.index('    EnsureResolved(',start(old))]; n=n[:a]+original+n[b:]; n=n.replace('HostYEdge(destRow)','HostY(destRow)'); self.assertEqual(n,old[start(old):end(old)])
 def test_only_recorded_after_draw(self):
  s=Path('src/gpu/renderer.cpp').read_text();a=s.index('bool BlitResolve(');b=s.index('// EDRAM resolve:',a);f=s[a:b]; self.assertEqual(f.count('return false;'),2);self.assertGreater(f.index('return true;'),f.index('DrawInstanced(3, 1, 0, 0)'));self.assertGreater(f.index('return true;'),f.index('g_drawState.Invalidate();'))
 def test_coverage_uses_recorded_clipped_scissor(self):
  s=Path('src/gpu/renderer.cpp').read_text(); i=s.index('const bool resolveRecorded = BlitResolve'); j=s.index('// Temporal upscaling:',i); t=s[i:j]; self.assertIn('const D3D12_RECT host=ToHost(',t); self.assertIn('recorded.resource.Get()',t); self.assertIn('recorded.hostWidth,recorded.hostHeight',t); self.assertIn('clamp(host.bottom,recorded.hostHeight),resolveRecorded',t); self.assertIn('g_explicitFrame && !depth',t)
if __name__=='__main__':unittest.main()
