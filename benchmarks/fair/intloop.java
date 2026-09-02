public class intloop{
  public static void main(String[] a){
    long n=50000000L; String e=System.getenv("BENCH_N");
    if(e!=null){ try{ long v=Long.parseLong(e); if(v>0)n=v; }catch(Exception x){} }
    long t=1; for(long i=0;i<n;i++) t=(t*31+i)%1000000007L;
    System.out.println(t);
  }
}
