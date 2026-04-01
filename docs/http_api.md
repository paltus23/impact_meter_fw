# Impact Meter — HTTP API Reference

The device runs an HTTP server on **port 80** (default ESP-IDF port).  
All API endpoints are under the `/api/` prefix.  
The root path `/` serves the built-in HTML visualizer page.

---

## Table of Contents

1. [Binary file format](#binary-file-format)
2. [GET /](#get-)
3. [GET /api/files](#get-apifiles)
4. [GET /api/files/\<name\>](#get-apifilesname)
5. [DELETE /api/files/\<name\>](#delete-apifilesname)
6. [DELETE /api/files](#delete-apifiles)
7. [GET /api/settings](#get-apisettings)
8. [POST /api/settings](#post-apisettings)

---

## Binary file format

Every capture profile begins with a **`profile_header_t`** (15 bytes), followed immediately by the raw sample payload.

### File layout

```
Offset  Size  Type      Field
------  ----  --------  -----
 0       4    uint32_t  magic       — 0x4C454341 ("ACEL", little-endian)
 4       1    uint8_t   version     — currently 1
 5       2    uint16_t  header_size — total header size in bytes; sample data starts here
 7       4    float32   odr_hz      — output data rate in Hz (e.g. 3200.0)
11       4    uint32_t  data_size   — sample payload size in bytes
15       …    int16_t…  samples     — XYZ int16 LE triplets (6 bytes each)
```

> **Extensibility:** if a future version increases `header_size`, readers must skip
> to byte `header_size` before reading samples. Fields beyond `data_size` are reserved.

### Sample struct

```c
typedef struct {
    int16_t x;   // 2 bytes
    int16_t y;   // 2 bytes
    int16_t z;   // 2 bytes
} adxl375_sample_t;   // 6 bytes total per sample
```

- One sample = **6 bytes** (3 × `int16_t`, little-endian).
- `data_size` is always a multiple of 6. Any trailing bytes should be ignored.
- Raw values are in ADXL375 LSBs. At the ±200 g full-scale range the scale factor is **49 mg/LSB**, so `g = raw × 0.049`.
- `odr_hz` carries the exact ODR used during the capture (typically `3200.0`).

### JavaScript parse example

```js
const PROFILE_HEADER_MAGIC = 0x4C454341;

function parseProfile(buf) {
  const view = new DataView(buf);

  // Detect header
  let sampleOffset = 0;
  let odrHz = 3200;  // fallback for legacy files without a header
  if (buf.byteLength >= 15 && view.getUint32(0, true) === PROFILE_HEADER_MAGIC) {
    sampleOffset = view.getUint16(5, true);   // header_size
    odrHz        = view.getFloat32(7, true);  // odr_hz
  }

  const payloadBytes = buf.byteLength - sampleOffset;
  const count = Math.floor(payloadBytes / 6);
  const samples = [];
  for (let i = 0; i < count; i++) {
    const base = sampleOffset + i * 6;
    samples.push({
      x: view.getInt16(base,     true),
      y: view.getInt16(base + 2, true),
      z: view.getInt16(base + 4, true),
    });
  }
  return { odrHz, samples };  // multiply values by 0.049 for g
}

async function fetchProfile(name) {
  const buf = await (await fetch(`/api/files/${name}`)).arrayBuffer();
  return parseProfile(buf);
}
```

---

## GET /

Serves the built-in accelerometer visualizer HTML page.

| | |
|---|---|
| **Method** | `GET` |
| **Path** | `/` |
| **Response** | `text/html` |

---

## GET /api/files

Returns a JSON array of all stored capture profile names.

| | |
|---|---|
| **Method** | `GET` |
| **Path** | `/api/files` |
| **Response** | `application/json` |

### Response body

```json
["capture_0.bin", "capture_1.bin", "capture_2.bin"]
```

An empty array `[]` is returned when no profiles are stored.

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Success |
| `500` | Internal error (filesystem failure) |

### Example

```js
const res = await fetch('/api/files');
const names = await res.json();   // string[]
```

---

## GET /api/files/\<name\>

Downloads a single capture profile as a raw binary file.  
The browser will receive a `Content-Disposition: attachment` header suggesting the original filename.

| | |
|---|---|
| **Method** | `GET` |
| **Path** | `/api/files/{name}` |
| **Path parameter** | `name` — exact profile name returned by `GET /api/files` |
| **Response** | `application/octet-stream` |

### Response headers

```
Content-Disposition: attachment; filename="<name>"
```

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Binary payload streams successfully |
| `400` | Name is empty |
| `404` | Profile does not exist |
| `500` | Internal error |

### Example

```js
const res = await fetch('/api/files/capture_0.bin');
const buf = await res.arrayBuffer();  // parse with DataView (see Binary file format above)
```

---

## DELETE /api/files/\<name\>

Permanently deletes a single capture profile.

| | |
|---|---|
| **Method** | `DELETE` |
| **Path** | `/api/files/{name}` |
| **Path parameter** | `name` — exact profile name |
| **Response** | `application/json` |

### Response body

```json
{"deleted": true}
```

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Profile deleted |
| `400` | Name is empty |
| `404` | Profile does not exist |
| `500` | Internal error |

### Example

```js
await fetch('/api/files/capture_0.bin', { method: 'DELETE' });
```

---

## DELETE /api/files

Deletes **all** stored capture profiles in one request.

| | |
|---|---|
| **Method** | `DELETE` |
| **Path** | `/api/files` |
| **Response** | `application/json` |

### Response body

```json
{"deleted": 3}
```

`deleted` is the count of successfully removed profiles. Profiles that could not be removed are skipped (count is still `200 OK`).

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Operation completed (check `deleted` count) |
| `500` | Could not enumerate profiles |

### Example

```js
const res  = await fetch('/api/files', { method: 'DELETE' });
const body = await res.json();  // { deleted: N }
```

---

## GET /api/settings

Returns the current device settings as a JSON object.

| | |
|---|---|
| **Method** | `GET` |
| **Path** | `/api/settings` |
| **Response** | `application/json` |

### Response body

```json
{
  "profile_num":  0,
  "precap_ms":    200,
  "capture_ms":   1000
}
```

### Fields

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `profile_num` | integer | `0` | `0 … INT32_MAX` | Index of the next capture profile to be written. Auto-increments after each capture. |
| `precap_ms` | integer | `200` | `0 … 5000` | Pre-event ring-buffer window in milliseconds. Samples recorded before the trigger event that will be prepended to the capture file. |
| `capture_ms` | integer | `1000` | `1 … 60000` | Post-event capture duration in milliseconds. |

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Success |
| `500` | Internal error |

### Example

```js
const res      = await fetch('/api/settings');
const settings = await res.json();
```

---

## POST /api/settings

Updates one or more settings. Only the keys present in the request body are updated; omitted keys are left unchanged.  
Returns the **full settings object** (same as `GET /api/settings`) reflecting the new state.

| | |
|---|---|
| **Method** | `POST` |
| **Path** | `/api/settings` |
| **Content-Type** | `application/json` |
| **Body limit** | 256 bytes |
| **Response** | `application/json` |

### Request body

Send any subset of the settings fields:

```json
{
  "precap_ms":  500,
  "capture_ms": 2000
}
```

### Response body

The updated full settings object (same schema as `GET /api/settings`).

```json
{
  "profile_num":  0,
  "precap_ms":    500,
  "capture_ms":   2000
}
```

### Status codes

| Code | Meaning |
|------|---------|
| `200` | Success — response body contains updated settings |
| `400` | Missing body, body too large (> 256 bytes), or invalid JSON |
| `500` | Internal error |

### Example

```js
const res = await fetch('/api/settings', {
  method:  'POST',
  headers: { 'Content-Type': 'application/json' },
  body:    JSON.stringify({ capture_ms: 2000, precap_ms: 500 }),
});
const updated = await res.json();
```

> **Note:** Changing any setting immediately fires an internal `CONFIG_CHANGED` event. The accelerometer task reloads all settings within one FIFO interrupt cycle, so the new values take effect within a few milliseconds.

---

## Quick-start snippet

```js
const BASE = 'http://192.168.x.x';   // replace with device IP

// List profiles
const names = await (await fetch(`${BASE}/api/files`)).json();

// Download and parse the first profile (header-aware)
const buf  = await (await fetch(`${BASE}/api/files/${names[0]}`)).arrayBuffer();
const view = new DataView(buf);
const MAGIC = 0x4C454341;
const sampleOffset = (buf.byteLength >= 15 && view.getUint32(0, true) === MAGIC)
  ? view.getUint16(5, true)   // skip header
  : 0;                        // legacy file — no header
const odrHz = (sampleOffset > 0) ? view.getFloat32(7, true) : 3200;
const count = Math.floor((buf.byteLength - sampleOffset) / 6);
const samples = [];
for (let i = 0; i < count; i++) {
  const b = sampleOffset + i * 6;
  samples.push({
    x: view.getInt16(b,     true) * 0.049,  // g
    y: view.getInt16(b + 2, true) * 0.049,
    z: view.getInt16(b + 4, true) * 0.049,
  });
}
console.log(`ODR: ${odrHz} Hz, samples: ${count}`);

// Update settings
await fetch(`${BASE}/api/settings`, {
  method:  'POST',
  headers: { 'Content-Type': 'application/json' },
  body:    JSON.stringify({ capture_ms: 3000 }),
});

// Delete all profiles
await fetch(`${BASE}/api/files`, { method: 'DELETE' });
```
