package main

import (
	"fmt"
	"math"
	"os"
	"strconv"
)

const NB = 5
const PI = 3.141592653589793
const SOLAR_MASS = 4 * PI * PI
const DPY = 365.24

var x = [NB]float64{0.0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01}
var y = [NB]float64{0.0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01}
var z = [NB]float64{0.0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01}
var vx = [NB]float64{0.0, 1.66007664274403694e-03*DPY, -2.76742510726862411e-03*DPY, 2.96460137564761618e-03*DPY, 2.68067772490389322e-03*DPY}
var vy = [NB]float64{0.0, 7.69901118419740425e-03*DPY, 4.99852801234917238e-03*DPY, 2.37847173959480950e-03*DPY, 1.62824170038242295e-03*DPY}
var vz = [NB]float64{0.0, -6.90460016972063023e-05*DPY, 2.30417297573763929e-05*DPY, -2.96589568540237556e-05*DPY, -9.51592254519715870e-05*DPY}
var mass = [NB]float64{SOLAR_MASS, 9.54791938424326609e-04*SOLAR_MASS, 2.85885980666130812e-04*SOLAR_MASS, 4.36624404335156298e-05*SOLAR_MASS, 5.15138902046611451e-05*SOLAR_MASS}

func advance(dt float64) {
	for i := 0; i < NB; i++ {
		for j := i + 1; j < NB; j++ {
			dx, dy, dz := x[i]-x[j], y[i]-y[j], z[i]-z[j]
			d2 := dx*dx + dy*dy + dz*dz
			mag := dt / (d2 * math.Sqrt(d2))
			vx[i] -= dx * mass[j] * mag
			vy[i] -= dy * mass[j] * mag
			vz[i] -= dz * mass[j] * mag
			vx[j] += dx * mass[i] * mag
			vy[j] += dy * mass[i] * mag
			vz[j] += dz * mass[i] * mag
		}
	}
	for i := 0; i < NB; i++ {
		x[i] += dt * vx[i]
		y[i] += dt * vy[i]
		z[i] += dt * vz[i]
	}
}

func energy() float64 {
	e := 0.0
	for i := 0; i < NB; i++ {
		e += 0.5 * mass[i] * (vx[i]*vx[i] + vy[i]*vy[i] + vz[i]*vz[i])
		for j := i + 1; j < NB; j++ {
			dx, dy, dz := x[i]-x[j], y[i]-y[j], z[i]-z[j]
			e -= (mass[i] * mass[j]) / math.Sqrt(dx*dx+dy*dy+dz*dz)
		}
	}
	return e
}

func main() {
	n := 1000000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	px, py, pz := 0.0, 0.0, 0.0
	for i := 0; i < NB; i++ {
		px += vx[i] * mass[i]
		py += vy[i] * mass[i]
		pz += vz[i] * mass[i]
	}
	vx[0], vy[0], vz[0] = -px/SOLAR_MASS, -py/SOLAR_MASS, -pz/SOLAR_MASS
	for i := 0; i < n; i++ {
		advance(0.01)
	}
	fmt.Println(int64(math.Round(energy() * 1e9)))
}
