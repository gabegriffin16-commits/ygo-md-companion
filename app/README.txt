Master Duel Companion App - desktop overlay source
==================================================
GitHub builds this automatically: change anything in app/, bump "version" in package.json,
push to main, and the "Build app" workflow posts MasterDuelCompanion.exe as a release.
The app checks for a newer release on start and shows an Update button in its top bar.

To build by hand (needs Node.js LTS):
  npm install
  npm run build      -> dist/MasterDuelCompanion.exe
To run without building:  npm start
The app loads the GitHub site (always current) and falls back to site/ when offline.
