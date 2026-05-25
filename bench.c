#include <stdio.h>

int fib(int n) {
	if (n < 2) return n;
	return fib(n - 1) + fib(n - 2);
}

int main() {
	printf("Calculating fib(40)...Wait!\n");
	int expr = fib(40);
	printf("%d \n", expr);
	return 0;
}