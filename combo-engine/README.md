# combo-engine

The helper program behind **Find combos** in the Master Duel Companion App. It plays a deck's turn one out in the
real Yu-Gi-Oh! rules engine and reports the end boards it can reach, with the steps to get there.

- License: **AGPL-3.0-or-later** (see `LICENSE`). This folder is the complete source of the helper. The app runs it as a
  separate program and talks to it over stdin/stdout, so the rest of the app is not part of this work.
- Rules engine: [edo9300/ygopro-core](https://github.com/edo9300/ygopro-core) (AGPL-3.0-or-later), built in at commit
  `38d04c9feb1a26617407091380634c87262fe3f8`, with its bundled Lua 5.4.
- Card scripts: [ProjectIgnis/CardScripts](https://github.com/ProjectIgnis/CardScripts) (AGPL-3.0). Not bundled; the app
  downloads them from GitHub on first use.
- Card database: `cards.cdb` from [ProjectIgnis/BabelCDB](https://github.com/ProjectIgnis/BabelCDB). Not bundled;
  downloaded on first use.
- Also built in: SQLite (public domain), miniz (MIT), nlohmann/json (MIT).

## Build

```
cmake -B build -DOCGCORE_DIR=<ygopro-core checkout, with the lua/src submodule> -DDEPS_DIR=<folder with json.hpp, miniz/, sqlite/>
cmake --build build --config Release
```

`.github/workflows/build-app.yml` shows the exact downloads used for the Windows build.

## Protocol

One JSON object per line in each direction.

```
{"id":1,"cmd":"init","cdb":"cards.cdb","scripts":"CardScripts.zip"}  -> {"id":1,"ready":true,"cards":N,"scripts":N}
{"id":2,"cmd":"search","deck":[codes],"extra":[codes],"hand":[codes],
 "maxActions":8,"timeMs":20000,"threads":2,"targets":[codes],"top":12}
   -> {"id":2,"progress":{...}} every half second, {"id":2,"warning":...,"cards":[...]} for unscripted cards,
      then {"id":2,"done":true,"complete":bool,"boards":[{score,field,backrow,hand,gy,banished,steps:[{do,card,effect,picks}]}],"stats":{...}}
{"cmd":"stop"}   ends the running search early; results so far are still sent
{"cmd":"quit"}
```

`combo-engine --version` prints the version.
