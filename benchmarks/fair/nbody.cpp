// N-body (5 cisimli gunes sistemi, Benchmarks Game surumu).
// Olculen: kayan nokta aritmetigi + kucuk sabit diziler + sqrt.
// matmul (depolama) ve mandelbrot (saf aritmetik) arasindaki gercekci orta nokta.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#define NB 5
#define PI 3.141592653589793
#define SOLAR_MASS (4 * PI * PI)
#define DPY 365.24
static double x[NB], y[NB], z[NB], vx[NB], vy[NB], vz[NB], mass[NB];
static void init(void) {
    double d[NB][7] = {
        {0,0,0,0,0,0,SOLAR_MASS},
        {4.84143144246472090e+00,-1.16032004402742839e+00,-1.03622044471123109e-01,
         1.66007664274403694e-03*DPY,7.69901118419740425e-03*DPY,-6.90460016972063023e-05*DPY,
         9.54791938424326609e-04*SOLAR_MASS},
        {8.34336671824457987e+00,4.12479856412430479e+00,-4.03523417114321381e-01,
        -2.76742510726862411e-03*DPY,4.99852801234917238e-03*DPY,2.30417297573763929e-05*DPY,
         2.85885980666130812e-04*SOLAR_MASS},
        {1.28943695621391310e+01,-1.51111514016986312e+01,-2.23307578892655734e-01,
         2.96460137564761618e-03*DPY,2.37847173959480950e-03*DPY,-2.96589568540237556e-05*DPY,
         4.36624404335156298e-05*SOLAR_MASS},
        {1.53796971148509165e+01,-2.59193146099879641e+01,1.79258772950371181e-01,
         2.68067772490389322e-03*DPY,1.62824170038242295e-03*DPY,-9.51592254519715870e-05*DPY,
         5.15138902046611451e-05*SOLAR_MASS}};
    for (int i = 0; i < NB; i++) {
        x[i]=d[i][0]; y[i]=d[i][1]; z[i]=d[i][2];
        vx[i]=d[i][3]; vy[i]=d[i][4]; vz[i]=d[i][5]; mass[i]=d[i][6];
    }
    double px=0,py=0,pz=0;
    for (int i = 0; i < NB; i++) { px+=vx[i]*mass[i]; py+=vy[i]*mass[i]; pz+=vz[i]*mass[i]; }
    vx[0]=-px/SOLAR_MASS; vy[0]=-py/SOLAR_MASS; vz[0]=-pz/SOLAR_MASS;
}
static void advance(double dt) {
    for (int i = 0; i < NB; i++)
        for (int j = i + 1; j < NB; j++) {
            double dx=x[i]-x[j], dy=y[i]-y[j], dz=z[i]-z[j];
            double d2=dx*dx+dy*dy+dz*dz;
            double mag=dt/(d2*sqrt(d2));
            vx[i]-=dx*mass[j]*mag; vy[i]-=dy*mass[j]*mag; vz[i]-=dz*mass[j]*mag;
            vx[j]+=dx*mass[i]*mag; vy[j]+=dy*mass[i]*mag; vz[j]+=dz*mass[i]*mag;
        }
    for (int i = 0; i < NB; i++) { x[i]+=dt*vx[i]; y[i]+=dt*vy[i]; z[i]+=dt*vz[i]; }
}
static double energy(void) {
    double e = 0.0;
    for (int i = 0; i < NB; i++) {
        e += 0.5*mass[i]*(vx[i]*vx[i]+vy[i]*vy[i]+vz[i]*vz[i]);
        for (int j = i + 1; j < NB; j++) {
            double dx=x[i]-x[j], dy=y[i]-y[j], dz=z[i]-z[j];
            e -= (mass[i]*mass[j])/sqrt(dx*dx+dy*dy+dz*dz);
        }
    }
    return e;
}
int main(void) {
    const char *e = getenv("BENCH_N");
    int n = e ? atoi(e) : 1000000;
    if (n <= 0) n = 1000000;
    init();
    for (int i = 0; i < n; i++) advance(0.01);
    printf("%lld\n", (long long)llround(energy() * 1e9));
    return 0;
}
