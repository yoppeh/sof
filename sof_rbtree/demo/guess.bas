10 REM Guess the number: the computer picks one from 1 to 100
20 PRINT "I'm thinking of a number from 1 to 100."
30 N = INT(RND * 100) + 1
40 T = 0
50 INPUT "Your guess"; G
60 T = T + 1
70 IF G = INT(G) AND G >= 1 AND G <= 100 THEN 100
80 PRINT "A whole number from 1 to 100, please."
90 GOTO 50
100 IF G < N THEN PRINT "Too low.": GOTO 50
110 IF G > N THEN PRINT "Too high.": GOTO 50
120 PRINT "Right! It was"; N; "and you took"; T; "guesses."
130 INPUT "Play again (y/n)"; A$
140 IF LEFT$(A$, 1) = "y" OR LEFT$(A$, 1) = "Y" THEN 20
150 PRINT "Goodbye."
160 END
