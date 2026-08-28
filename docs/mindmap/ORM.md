---
tags: [stdlib, db]
---

# ORM

`lib/orm.tpr` — `db_execute`/`db_query` üzerine ince sarmalayıcı.

## Bilinmesi gerekenler
- Tek **global `_orm_db`** handle (paylaşımlı handle deseni → SQLite iç mutex serileştirmesi, [[SQLite and DB]]).
- `orm_open(path)` → `db_open`; insert → `db_last_insert_id`.
- Verbose `db_*` dansını sadeleştirir.

## İlgili
[[SQLite and DB]] · [[Standard Library]] · [[Performance]]
