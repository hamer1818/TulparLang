public class strcat{
  public static void main(String[] a){
    long n=2000000; String e=System.getenv("BENCH_N");
    if(e!=null){ try{ long v=Long.parseLong(e); if(v>0)n=v; }catch(Exception x){} }
    StringBuilder b=new StringBuilder();
    for(long i=0;i<n;i++){ b.append(i%1000); b.append(','); }
    String s=b.toString();
    long cnt=0; for(int i=0;i<s.length();i++) if(s.charAt(i)==',') cnt++;
    System.out.println(s.length()+" "+cnt);
  }
}
