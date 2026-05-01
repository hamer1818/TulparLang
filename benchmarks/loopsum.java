public class loopsum {
    public static void main(String[] args) {
        long total = 0;
        for (long i = 0; i < 10_000_000L; i++) {
            total += i;
        }
        System.out.println(total);
    }
}
