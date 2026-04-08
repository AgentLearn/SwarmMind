/// 3-component vector used for all spatial math in the simulation.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Vec3 {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

impl Vec3 {
    #[inline] pub fn new(x: f64, y: f64, z: f64) -> Self { Self { x, y, z } }

    #[inline] pub fn add(self, o: Self) -> Self { Self::new(self.x+o.x, self.y+o.y, self.z+o.z) }
    #[inline] pub fn sub(self, o: Self) -> Self { Self::new(self.x-o.x, self.y-o.y, self.z-o.z) }
    #[inline] pub fn scale(self, s: f64)  -> Self { Self::new(self.x*s, self.y*s, self.z*s) }

    #[inline] pub fn len_sq(self) -> f64 { self.x*self.x + self.y*self.y + self.z*self.z }
    #[inline] pub fn len(self)    -> f64 { self.len_sq().sqrt() }

    #[inline]
    pub fn norm(self) -> Self {
        let l = self.len();
        if l < 1e-9 { Self::default() } else { self.scale(1.0 / l) }
    }

    #[inline] pub fn dist(self, o: Self)    -> f64 { self.sub(o).len() }
    #[inline] pub fn dist_sq(self, o: Self) -> f64 { self.sub(o).len_sq() }

    /// Cross product — used to compute perpendicular flanking direction.
    #[inline]
    pub fn cross(self, o: Self) -> Self {
        Self::new(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )
    }

    /// Linear interpolation: `self + (o - self) * t`
    #[inline]
    pub fn lerp(self, o: Self, t: f64) -> Self {
        self.add(o.sub(self).scale(t))
    }
}
