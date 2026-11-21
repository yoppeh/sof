10 REM Kepler's third law: a planet's year from its distance to the sun
20 DEF FNY(A) = A ^ 1.5
30 DEF FNR(X, D) = INT(X * 10 ^ D + .5) / 10 ^ D
40 PRINT "Planet"; TAB(12); "Distance (AU)"; TAB(28); "Year (Earth years)"; TAB(48); "Year (days)"
50 FOR I = 1 TO 8
60 READ P$, A
70 Y = FNY(A)
80 PRINT P$; TAB(12); A; TAB(28); FNR(Y, 3); TAB(48); INT(Y * 365.25 + .5)
90 NEXT I
100 RESTORE
110 READ P$, A
120 PRINT
130 PRINT "Closest: "; P$; ", whose year is"; FNR(FNY(A) * 365.25, 1); "days."
140 DATA Mercury, .387, Venus, .723, Earth, 1, Mars, 1.524
150 DATA Jupiter, 5.203, Saturn, 9.537, Uranus, 19.19, Neptune, 30.07
160 END
