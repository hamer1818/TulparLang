public class arrayiter{
  public static void main(String[] s){
    int n=5000000; String e=System.getenv("BENCH_N");
    if(e!=null){ try{ int v=Integer.parseInt(e); if(v>0)n=v; }catch(Exception x){} }
    long[] a=new long[n]; long t=0;
    for(int i=0;i<a.length;i++) a[i]=i;
    for(int i=0;i<a.length;i++) t+=a[i];
    System.out.println(t);
  }
}
