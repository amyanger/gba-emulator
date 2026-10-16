//! Deterministic randomness for the robustness tests (no crates).

pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng(seed | 1)
    }

    pub fn next_u64(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }

    pub fn below(&mut self, n: usize) -> usize {
        (self.next_u64() % n as u64) as usize
    }

    /// Overwrites `flips` random bytes of `buf` with random values.
    pub fn mutate(&mut self, buf: &mut [u8], flips: usize) {
        for _ in 0..flips {
            let i = self.below(buf.len());
            buf[i] = self.next_u64() as u8;
        }
    }
}
