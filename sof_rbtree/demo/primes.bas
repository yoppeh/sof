10 REM The primes up to 300, by the sieve of Eratosthenes
20 M = 300
30 DIM S(300)
40 C = 0
50 FOR I = 2 TO M
60 IF S(I) THEN 100
70 PRINT I;
80 C = C + 1
90 FOR J = I * I TO M STEP I: S(J) = 1: NEXT J
100 NEXT I
110 PRINT
120 PRINT C; "primes up to"; M
130 END
