10 REM Hangman: guess the word a letter at a time, with six misses allowed
20 DIM W$(8)
30 FOR I = 1 TO 8: READ W$(I): NEXT I
40 W$ = W$(INT(RND * 8) + 1)
50 L = LEN(W$): G$ = "": M = 0
60 REM Show the word, with the letters not yet guessed as dashes
70 S$ = "": H = 0
80 FOR I = 1 TO L
90 C$ = MID$(W$, I, 1)
100 IF INSTR(G$, C$) > 0 THEN S$ = S$ + C$: H = H + 1: GOTO 120
110 S$ = S$ + "-"
120 NEXT I
130 PRINT S$; "   misses:"; M; "of 6"
140 IF H = L THEN PRINT "You got it: "; W$: GOTO 250
150 IF M = 6 THEN PRINT "Hanged! The word was "; W$: GOTO 250
160 INPUT "Letter"; A$
170 IF LEN(A$) <> 1 THEN PRINT "One letter, please.": GOTO 160
180 IF ASC(A$) >= 65 AND ASC(A$) <= 90 THEN A$ = CHR$(ASC(A$) + 32)
190 IF INSTR(G$, A$) > 0 THEN PRINT "You tried "; A$; " already.": GOTO 160
200 G$ = G$ + A$
210 IF INSTR(W$, A$) = 0 THEN M = M + 1: PRINT "No "; A$; "."
220 GOTO 70
250 INPUT "Another (y/n)"; A$
260 IF A$ = "y" THEN 40
270 END
280 DATA basic, program, variable, subroutine, interpreter, string, number, keyboard
