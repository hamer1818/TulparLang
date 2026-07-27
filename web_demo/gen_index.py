#!/usr/bin/env python3
# web_demo/index.html üreticisi — iki dilli sayfa + TR/EN kod görüntüleyici.
# Kaynaklar examples/'tan OKUNUR (tek doğru kaynak), JSON olarak gömülür.
# Oyun kodunu değiştirdikten sonra:  python3 web_demo/gen_index.py
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EX = os.path.join(ROOT, "examples")

games = [
    dict(tr="zipla", en="jump", emoji="🦘", name_tr="Zıpla", name_en="Jump",
         src_tr="arcade_zipla.tpr", src_en="en/jump.tpr",
         d_tr="Platformer — Sol/Sağ veya A/D: yürü, SPACE/YUKARI: zıpla; altın hedefe ulaş.",
         d_en="Platformer — Left/Right or A/D: walk, SPACE/UP: jump; reach the gold target.",
         lv_tr="kolay → zikzak tırmanış → dar platformlar + hareketli engel",
         lv_en="easy → zigzag climb → narrow platforms + moving obstacle"),
    dict(tr="topla", en="collect", emoji="💰", name_tr="Topla", name_en="Collect",
         src_tr="arcade_topla.tpr", src_en="en/collect.tpr",
         d_tr="Ok tuşları / WASD ile sür; tüm sarıları topla, kırmızıya değme.",
         d_en="Arrow keys / WASD to drive; collect all yellows, avoid red.",
         lv_tr="3 eşya / 1 düşman → 5 / 2 → 6 / 3 hızlı",
         lv_en="3 items / 1 enemy → 5 / 2 → 6 / 3 fast"),
    dict(tr="nisan", en="shooter", emoji="🎯", name_tr="Nişancı", name_en="Shooter",
         src_tr="arcade_nisan.tpr", src_en="en/shooter.tpr",
         d_tr="Ok / WASD: hareket, SPACE: ateş; inen kırmızıları vur.",
         d_en="Arrows / WASD: move, SPACE: fire; shoot the falling reds.",
         lv_tr="hedef skor 30 → 60 → 100 (tempo artar)",
         lv_en="target score 30 → 60 → 100 (tempo rises)"),
    dict(tr="yilan", en="snake", emoji="🐍", name_tr="Yılan", name_en="Snake",
         src_tr="tame_snake.tpr", src_en="en/snake.tpr",
         d_tr="Ok tuşları / WASD ile yönlendir; yemi ye, kendine çarpma.",
         d_en="Arrow keys / WASD to steer; eat food, don't hit yourself.",
         lv_tr="her bölümde 5 yem — yavaş → hızlı → en hızlı + engeller",
         lv_en="5 food per level — slow → fast → fastest + obstacles"),
    dict(tr="tugla", en="breakout", emoji="🧱", name_tr="Tuğla Kırma", name_en="Breakout",
         src_tr="arcade_tugla.tpr", src_en="en/breakout.tpr",
         d_tr="Sol/Sağ veya A/D: raket; topu düşürmeden tüm tuğlaları kır.",
         d_en="Left/Right or A/D: paddle; break all bricks without dropping the ball.",
         lv_tr="düz duvar → piramit → boşluklu duvar + hızlı top",
         lv_en="flat wall → pyramid → gapped wall + faster ball"),
    dict(tr="uzay", en="invaders", emoji="👾", name_tr="Uzay İstilası", name_en="Space Invaders",
         src_tr="arcade_uzay.tpr", src_en="en/invaders.tpr",
         d_tr="Sol/Sağ veya A/D: gemi, SPACE: ateş; filoyu temizle.",
         d_en="Left/Right or A/D: ship, SPACE: fire; clear the fleet.",
         lv_tr="3×5 yavaş → 4×6 hızlı → 4×7 + filo geri ateş eder",
         lv_en="3×5 slow → 4×6 fast → 4×7 + the fleet shoots back"),
    dict(tr="labirent", en="maze", emoji="🗝️", name_tr="Labirent", name_en="Maze",
         src_tr="arcade_labirent.tpr", src_en="en/maze.tpr",
         d_tr="Ok tuşları / WASD; devriyelere yakalanmadan tüm anahtarları topla.",
         d_en="Arrow keys / WASD; collect all keys without being caught by patrols.",
         lv_tr="geniş → simetrik / 2 devriye → sık / 3 hızlı devriye",
         lv_en="open → symmetric / 2 patrols → dense / 3 fast patrols"),
    dict(tr="karsiya", en="crossing", emoji="🚗", name_tr="Karşıya Geç", name_en="Crossing",
         src_tr="arcade_karsiya.tpr", src_en="en/crossing.tpr",
         d_tr="Ok tuşları / WASD; akan araçlara değmeden altın şeride ulaş.",
         d_en="Arrow keys / WASD; reach the gold strip without touching the flowing cars.",
         lv_tr="3 şerit yavaş → 4 şerit → 5 şerit hızlı",
         lv_en="3 lanes slow → 4 lanes → 5 fast lanes"),
    dict(tr="ucus", en="flight", emoji="🐦", name_tr="Uçuş", name_en="Flight",
         src_tr="arcade_ucus.tpr", src_en="en/flight.tpr",
         d_tr="SPACE / YUKARI: kanat çırp; boruların arasından geç.",
         d_en="SPACE / UP: flap; fly through the pipe gaps.",
         lv_tr="geniş boşluk / yavaş → dar / hızlı → en dar / en hızlı",
         lv_en="wide gap / slow → narrow / fast → narrowest / fastest"),
    dict(tr="goktasi", en="dodge", emoji="☄️", name_tr="Göktaşı", name_en="Meteor Dodge",
         src_tr="arcade_goktasi.tpr", src_en="en/dodge.tpr",
         d_tr="Sol/Sağ veya A/D; düşen göktaşlarından sakın, süreyi doldur.",
         d_en="Left/Right or A/D; avoid the falling meteors, run out the clock.",
         lv_tr="12 sn / seyrek → 15 sn / sık → 18 sn / en yoğun",
         lv_en="12s / sparse → 15s / dense → 18s / densest"),
    dict(tr="kac", en="blockdodge", emoji="🟦", name_tr="Kaç — struct", name_en="Dodge — struct",
         src_tr="41_struct_entities.tpr", src_en="en/struct_entities.tpr",
         d_tr="Sol/Sağ veya A/D; düşen bloklardan kaç. arcade motoru DEĞİL, doğrudan tame + gerçek struct entity'ler.",
         d_en="Left/Right or A/D; dodge the falling blocks. NOT the arcade engine — direct tame + real struct entities.",
         lv_tr="paralel dizi / handle / context-global HİLESİ YOK — struct Entity bir dizide, üstünde metotlar, iç içe Vec alanları (kodu aç, gör).",
         lv_en="NO parallel-array / handle / context-global tricks — a struct Entity in an array, methods on it, nested Vec fields (open the code and see)."),
    dict(tr="2048", en="game2048", emoji="🔢", name_tr="2048", name_en="2048",
         src_tr="arcade_2048.tpr", src_en="en/game2048.tpr",
         d_tr="Parmağınla kaydır (mobil) / ok tuşları; aynı sayılar birleşir. Hamle kalmayınca biter.",
         d_en="Swipe with your finger (mobile) / arrow keys; equal numbers merge. Ends when no move is left.",
         lv_tr="sonsuz — skor birleşen sayıların toplamı; yıldız eşiği 100 / 400 / 1000",
         lv_en="endless — score is the sum of merged numbers; star thresholds 100 / 400 / 1000"),
    dict(tr="pong", en="pongen", emoji="🏓", name_tr="Pong", name_en="Pong",
         src_tr="arcade_pong.tpr", src_en="en/pong.tpr",
         d_tr="Yukarı/Aşağı veya W/S (mobilde ok butonları): sol raket; topu kaçırma. AI yenilebilir.",
         d_en="Up/Down or W/S (arrow buttons on mobile): left paddle; don't miss the ball. The AI is beatable.",
         lv_tr="sonsuz — her iade +1; top hızlanır; yıldız eşiği 5 / 15 / 30",
         lv_en="endless — +1 per return; the ball speeds up; star thresholds 5 / 15 / 30"),
    dict(tr="vur", en="whack", emoji="🔨", name_tr="Vur", name_en="Whack",
         src_tr="arcade_vur.tpr", src_en="en/whack.tpr",
         d_tr="Yanan kareye zamanında dokun (+1); köstebek kaçarsa biter. Her vuruşta hızlanır.",
         d_en="Tap the lit cell in time (+1); if the mole escapes it ends. It speeds up on every hit.",
         lv_tr="sonsuz — saf refleks (dokunma); yıldız eşiği 10 / 25 / 50",
         lv_en="endless — pure reflex (tap); star thresholds 10 / 25 / 50"),
]

# Kaynakları oku ve gömülecek JSON'u kur (anahtar = html taban adı).
sources = {}
for g in games:
    with open(os.path.join(EX, g["src_tr"]), encoding="utf-8") as f:
        sources[g["tr"]] = f.read()
    with open(os.path.join(EX, g["src_en"]), encoding="utf-8") as f:
        sources[g["en"]] = f.read()

cards = []
for g in games:
    cards.append(f'''  <article class="card" data-game>
    <div class="head">
      <span class="emoji">{g["emoji"]}</span>
      <h3><span data-tr>{g["name_tr"]}</span><span data-en>{g["name_en"]}</span></h3>
    </div>
    <p class="desc"><span data-tr>{g["d_tr"]}</span><span data-en>{g["d_en"]}</span></p>
    <p class="lv"><span class="tag">3</span>
      <span data-tr>bölüm: {g["lv_tr"]}</span><span data-en>levels: {g["lv_en"]}</span></p>
    <div class="actions">
      <a class="play" href="{g["tr"]}.html">▶ <span data-tr>Oyna (TR)</span><span data-en>Play (TR)</span></a>
      <a class="play" href="{g["en"]}.html">▶ <span data-tr>Oyna (EN)</span><span data-en>Play (EN)</span></a>
      <button class="codebtn" data-tr-key="{g["tr"]}" data-en-key="{g["en"]}">&lt;/&gt;
        <span data-tr>Kodu gör</span><span data-en>See code</span></button>
    </div>
    <div class="code" hidden>
      <div class="tabs">
        <button class="tab active" data-lang="tr">TulparLang · TR</button>
        <button class="tab" data-lang="en">TulparLang · EN</button>
      </div>
      <pre class="src"></pre>
    </div>
  </article>''')

cards_html = "\n".join(cards)
sources_json = json.dumps(sources, ensure_ascii=False)

html = f'''<!doctype html>
<html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tulpar — Web Oyunları / Web Games</title>
<style>
  :root{{--bg:#14141e;--card:#1e1e30;--card2:#22223a;--bd:#33335a;--gold:#ffd45e;
        --fg:#eee;--dim:#9a9ab5;--acc:#7ee081;--sky:#8fd0ff}}
  *{{box-sizing:border-box}}
  body{{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--fg);
       padding:32px 16px 60px}}
  .wrap{{max-width:860px;margin:0 auto}}
  header{{text-align:center;margin-bottom:8px}}
  h1{{font-size:24px;margin:0 0 6px}}
  .lead{{color:var(--dim);font-size:14px;margin:0 auto 16px;max-width:600px}}
  .langbar{{display:inline-flex;gap:0;border:1px solid var(--bd);border-radius:8px;
           overflow:hidden;margin-bottom:24px}}
  .langbar button{{background:var(--card2);color:var(--fg);border:0;padding:8px 18px;
           font:600 14px system-ui;cursor:pointer}}
  .langbar button.active{{background:var(--gold);color:#1a1a10}}
  h2{{font-size:13px;color:var(--dim);text-transform:uppercase;letter-spacing:.09em;
     margin:28px 0 12px;border-bottom:1px solid var(--bd);padding-bottom:6px}}
  .card{{background:var(--card);border:1px solid var(--bd);border-radius:12px;
        padding:16px 18px;margin-bottom:14px}}
  .head{{display:flex;align-items:center;gap:10px}}
  .emoji{{font-size:26px;line-height:1}}
  .card h3{{margin:0;font-size:18px;color:var(--gold)}}
  .desc{{margin:8px 0 4px;font-size:14px}}
  .lv{{margin:4px 0 12px;font-size:13px;color:var(--dim)}}
  .tag{{display:inline-block;background:#2b3b2b;color:var(--acc);font-weight:700;
       border-radius:5px;padding:0 7px;margin-right:4px}}
  .actions{{display:flex;flex-wrap:wrap;gap:8px;align-items:center}}
  .play,.codebtn{{font:600 13px system-ui;border-radius:8px;padding:8px 14px;
       text-decoration:none;cursor:pointer;border:1px solid var(--bd)}}
  .play{{background:var(--card2);color:var(--gold)}}
  .play:hover{{background:#2c2c4a}}
  .codebtn{{background:transparent;color:var(--sky)}}
  .codebtn:hover{{background:var(--card2)}}
  .code{{margin-top:14px;border:1px solid var(--bd);border-radius:8px;overflow:hidden}}
  .tabs{{display:flex;background:#191926;border-bottom:1px solid var(--bd)}}
  .tab{{background:transparent;color:var(--dim);border:0;padding:8px 14px;font:600 12px
       system-ui;cursor:pointer}}
  .tab.active{{color:var(--gold);box-shadow:inset 0 -2px 0 var(--gold)}}
  pre.src{{margin:0;padding:14px 16px;overflow-x:auto;font:13px/1.5 ui-monospace,
       "Cascadia Code",Consolas,monospace;color:#dbe3f0;background:#12121c;
       max-height:460px;white-space:pre}}
  footer{{text-align:center;color:var(--dim);font-size:13px;margin-top:36px}}
  a.ext{{color:var(--sky)}}
  /* dil görünürlüğü */
  body[data-lang="tr"] [data-en]{{display:none}}
  body[data-lang="en"] [data-tr]{{display:none}}
</style></head>
<body data-lang="tr">
<div class="wrap">
<header>
  <h1>Tulpar — <span data-tr>Web Oyunları</span><span data-en>Web Games</span></h1>
  <p class="lead">
    <span data-tr>TulparLang ile yazılıp WebAssembly'ye derlenmiş oyunlar. Her oyunun
      kaynağını <b>hem Türkçe hem İngilizce</b> API ile görebilirsin — TulparLang'in
      her fonksiyonunun iki dilde adı var. Bölümlü oyunlar 6 bölüm; hepsini bitir → KAZANDIN.</span>
    <span data-en>Games written in TulparLang and compiled to WebAssembly. You can view each
      game's source in <b>both Turkish and English</b> API — every TulparLang function has a
      name in both languages. Leveled games have 6 levels; clear them all → YOU WIN.</span>
  </p>
  <div class="langbar">
    <button data-setlang="tr" class="active">Türkçe</button>
    <button data-setlang="en">English</button>
  </div>
</header>

<h2><span data-tr>Android — telefonda oyna</span><span data-en>Android — play on your phone</span></h2>
<div class="card" style="border-color:var(--gold)">
  <div class="head"><span class="emoji">📱</span>
    <h3>Tulpar Arcade</h3></div>
  <p class="desc">
    <span data-tr>10 mini oyunun tamamı tek APK'da — telefonunda <b>native</b> çalışır
      (Play Store gerekmez). Dokunmatik kontroller oyuna göre değişir: joystick,
      ok butonları ya da kaydırma jestleri; donanım geri tuşu menüye döner.</span>
    <span data-en>All 10 mini games in a single APK — runs <b>natively</b> on your phone
      (no Play Store needed). Touch controls adapt per game: joystick, arrow
      buttons or swipe gestures; the hardware back button returns to the menu.</span></p>
  <p class="lv">
    <span data-tr>Kurulum: APK'yı indir → aç → "bilinmeyen uygulamalara izin ver" onayı → kur. Android 8.0+ (arm64).</span>
    <span data-en>Install: download the APK → open it → allow installs from unknown sources → install. Android 8.0+ (arm64).</span></p>
  <div class="actions">
    <a class="play" href="tulpar-arcade.apk" download>⬇ <span data-tr>APK indir</span><span data-en>Download APK</span> · 9&nbsp;MB</a>
  </div>
</div>

<h2><span data-tr>Oyunlar</span><span data-en>Games</span></h2>
{cards_html}

<h2><span data-tr>Tame demoları</span><span data-en>Tame demos</span></h2>
<div class="card">
  <div class="actions">
    <a class="play" href="mini.html">▶ mini</a>
    <a class="play" href="sprite.html">▶ sprite</a>
    <a class="play" href="rundemo.html">▶ rundemo</a>
  </div>
  <p class="lv" style="margin-top:10px">
    <span data-tr>Düşük seviye tame örnekleri (elle döngü, sprite, run()).</span>
    <span data-en>Low-level tame samples (manual loop, sprite, run()).</span></p>
</div>

<footer>
  <span data-tr>Kaynak:</span><span data-en>Source:</span>
  <a class="ext" href="https://tulparlang.dev">tulparlang.dev</a>
</footer>
</div>

<script type="application/json" id="sources">{sources_json}</script>
<script>
const SRC = JSON.parse(document.getElementById('sources').textContent);
// Sayfa dili
document.querySelectorAll('[data-setlang]').forEach(b=>{{
  b.addEventListener('click',()=>{{
    document.body.dataset.lang=b.dataset.setlang;
    document.querySelectorAll('[data-setlang]').forEach(x=>x.classList.toggle('active',x===b));
  }});
}});
// Kod görüntüleyici
document.querySelectorAll('[data-game]').forEach(card=>{{
  const btn=card.querySelector('.codebtn');
  const panel=card.querySelector('.code');
  const pre=card.querySelector('pre.src');
  const keys={{tr:btn.dataset.trKey,en:btn.dataset.enKey}};
  let lang='tr';
  const render=()=>{{pre.textContent=SRC[keys[lang]]||'(kaynak yok)';}};
  btn.addEventListener('click',()=>{{
    panel.hidden=!panel.hidden;
    if(!panel.hidden) render();
  }});
  card.querySelectorAll('.tab').forEach(t=>{{
    t.addEventListener('click',()=>{{
      lang=t.dataset.lang;
      card.querySelectorAll('.tab').forEach(x=>x.classList.toggle('active',x===t));
      render();
    }});
  }});
}});
</script>
</body></html>
'''

out = os.path.join(ROOT, "web_demo", "index.html")
with open(out, "w", encoding="utf-8") as f:
    f.write(html)
print("wrote", out, len(html), "bytes;", len(sources), "sources embedded")
