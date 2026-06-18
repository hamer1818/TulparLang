# ============================================================
#  KARŞILAŞTIRMA:  FastAPI (Python)  —  Users CRUD REST API
#  Aynı işi yapan Wings sürümü:       tests/compare_wings_users_api.tpr
#  Kurulum:   pip install "fastapi[standard]"
#  Çalıştır:  uvicorn tests.compare_fastapi_users_api:app --port 8080
#             (veya:  fastapi dev tests/compare_fastapi_users_api.py)
# ============================================================

from fastapi import FastAPI, HTTPException, status
from pydantic import BaseModel

app = FastAPI()

# --- "Veritabanı" (bellek içi) ---
_users = [{"id": 1, "name": "Ada"}, {"id": 2, "name": "Hamza"}]
_next_id = 3


# --- İstek/yanıt şemaları (FastAPI doğrulama + dökümantasyon bunlara dayanır) ---
class UserIn(BaseModel):
    name: str


class UserOut(BaseModel):
    id: int
    name: str


# --- Yardımcı: id'ye göre kullanıcı indexi (yoksa -1) ---
def _index_of(user_id: int) -> int:
    for i, u in enumerate(_users):
        if u["id"] == user_id:
            return i
    return -1


# GET /users  → hepsini listele
@app.get("/users")
def list_users() -> list[UserOut]:
    return _users


# GET /users/{id}  → tek kullanıcı (yoksa 404)
@app.get("/users/{user_id}")
def get_user(user_id: int) -> UserOut:
    idx = _index_of(user_id)
    if idx == -1:
        raise HTTPException(status_code=404, detail=f"user {user_id} not found")
    return _users[idx]


# POST /users  → oluştur (name zorunlu, 201 döner)
@app.post("/users", status_code=status.HTTP_201_CREATED)
def create_user(body: UserIn) -> UserOut:
    global _next_id
    if len(body.name) == 0:
        raise HTTPException(status_code=400, detail="name required")
    u = {"id": _next_id, "name": body.name}
    _next_id += 1
    _users.append(u)
    return u


# PUT /users/{id}  → güncelle (yoksa 404)
@app.put("/users/{user_id}")
def update_user(user_id: int, body: UserIn) -> UserOut:
    idx = _index_of(user_id)
    if idx == -1:
        raise HTTPException(status_code=404, detail=f"user {user_id} not found")
    if len(body.name) == 0:
        raise HTTPException(status_code=400, detail="name required")
    _users[idx]["name"] = body.name
    return _users[idx]


# DELETE /users/{id}  → sil (yoksa 404, varsa 204)
@app.delete("/users/{user_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_user(user_id: int) -> None:
    global _users
    idx = _index_of(user_id)
    if idx == -1:
        raise HTTPException(status_code=404, detail=f"user {user_id} not found")
    _users = [u for u in _users if u["id"] != user_id]
