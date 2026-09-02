public class sieve{
  public static void main(String[] a){
    int n=5000000; String e=System.getenv("BENCH_N");
    if(e!=null){ try{ int v=Integer.parseInt(e); if(v>0)n=v; }catch(Exception x){} }
    int[] f=new int[n+1]; long c=0;
    for(int i=2;i<=n;i++){ if(f[i]==0){ c++; for(long k=(long)i*i;k<=n;k+=i) f[(int)k]=1; } }
    System.out.println(c);
  }
}
