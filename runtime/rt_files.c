/* Read-only TempleOS files: the translator embeds the few a game needs (__tos_files);
   there is no disk image on the host.  shortcut: no writes, no directory listings beyond
   exact names; add files to EMBED_FILES in tools/hc_build.py when a game reads more. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "rt.h"

extern const TosFile __tos_files[];       /* lib: files every game reads */
extern const TosFile __tos_game_files[];  /* this game's own data files */

static int64_t opts[4];   /* Option() bits */

/* "::/Doc/X.DD", "C:/Doc/X.DD*", "/Doc/X.DD.Z" -> "Doc/X.DD" */
static void norm(const char *in, char *out, size_t cap)
{
  size_t n;
  if (!strncmp(in, "::/", 3)) in += 3;
  else if (isalpha((unsigned char)in[0]) && in[1] == ':') in += 2;
  if (!strncmp(in, "~/", 2)) { in += 2; }
  while (*in == '/' || *in == '\\') in++;
  strncpy(out, in, cap - 1);
  out[cap - 1] = 0;
  for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
  n = strlen(out);
  while (n && out[n - 1] == '*') out[--n] = 0;
  if (n > 2 && !_stricmp(out + n - 2, ".Z")) out[n - 2] = 0;
}

static uint8_t *str_new(const char *s)
{
  uint8_t *r = h_MAlloc((int64_t)strlen(s) + 1, NULL);
  strcpy((char *)r, s);
  return r;
}

static const TosFile *lookup(const uint8_t *name)
{
  const TosFile *tables[2] = {__tos_game_files, __tos_files};
  char want[512];
  const char *base;
  if (!name) return NULL;
  norm((const char *)name, want, sizeof want);
  for (int t = 0; t < 2; t++)
    for (const TosFile *f = tables[t]; f->name; f++)
      if (!_stricmp(f->name, want)) return f;
  if (strchr(want, '/')) return NULL;
  for (int t = 0; t < 2; t++)
    for (const TosFile *f = tables[t]; f->name; f++) {   /* relative name: match the file part */
      base = strrchr(f->name, '/');
      if (!_stricmp(base ? base + 1 : f->name, want)) return f;
    }
  return NULL;
}

uint8_t *h_DirCur(c_CTask *task, c_CTask *mem_task) { (void)task; (void)mem_task; return str_new("C:/Home"); }
void h_LoadDocDefines(void) {}   /* only fills in DolDoc text macros (OS version, sizes) */

uint8_t *h_FileRead(uint8_t *filename, int64_t *_size, int64_t *_attr)
{
  const TosFile *f = lookup(filename);
  uint8_t *res;
  if (_size) *_size = 0;
  if (_attr) *_attr = 0;
  if (!f) return NULL;
  res = h_MAlloc(f->size + 1, NULL);   /* MAlloc'd: callers Free it */
  memcpy(res, f->data, (size_t)f->size);
  res[f->size] = 0;
  if (_size) *_size = f->size;
  return res;
}

static void fill(c_CDirEntry *de, const TosFile *f)
{
  const char *base = strrchr(f->name, '/');
  memset(de, 0, sizeof *de);
  base = base ? base + 1 : f->name;
  strncpy((char *)de->m_name, base, sizeof de->m_name - 1);
  de->m_size = f->size;
  de->m_full_name = h_MAlloc((int64_t)strlen(f->name) + 4, NULL);
  strcpy((char *)de->m_full_name, "C:/");
  strcat((char *)de->m_full_name, f->name);
}

int8_t h_FileFind(uint8_t *filename, c_CDirEntry *de, int64_t fuf_flags)
{
  const TosFile *f = lookup(filename);
  (void)fuf_flags;
  if (f && de) fill(de, f);
  return f != NULL;
}

c_CDirEntry *h_FilesFind(uint8_t *mask, int64_t fuf_flags)
{
  const TosFile *f = lookup(mask);
  c_CDirEntry *de;
  (void)fuf_flags;
  if (!f) return NULL;
  de = (c_CDirEntry *)h_MAlloc(sizeof *de, NULL);
  fill(de, f);
  return de;
}

void h_DirEntryDel(c_CDirEntry *de)
{
  if (!de) return;
  h_Free(de->m_full_name);
  h_Free((uint8_t *)de);
}

void h_DirTreeDel(c_CDirEntry *de)
{
  c_CDirEntry *next;
  for (; de; de = next) {
    next = de->m_next;
    h_DirTreeDel(de->m_sub);
    h_DirEntryDel(de);
  }
}

int8_t h_Cd(uint8_t *dirname, int8_t make_dirs) { (void)dirname; (void)make_dirs; return 1; }


uint8_t *h_FileNameAbs(uint8_t *filename, int64_t fuf_flags)
{
  char buf[520];
  (void)fuf_flags;
  strcpy(buf, "C:/");
  norm(filename ? (const char *)filename : "", buf + 3, sizeof buf - 3);
  return str_new(buf);
}

uint8_t *h_ExtDft(uint8_t *filename, uint8_t *extension)
{//add ".ext" when the file part has no extension
  char buf[520];
  const char *base = strrchr((const char *)filename, '/');
  if (strchr(base ? base : (const char *)filename, '.') || strlen((const char *)filename) + strlen((const char *)extension) + 2 > sizeof buf)
    return str_new((const char *)filename);
  strcpy(buf, (const char *)filename);
  strcat(buf, ".");
  strcat(buf, (const char *)extension);
  return str_new(buf);
}

c_CBlkDev *h_DrvIsWritable(uint8_t drv_let, int8_t except) { (void)drv_let; (void)except; return NULL; }
void h_DrvsRelease(void) {}      /* a task ending releases the drives it locked: there are none */
void h_BlkDevsRelease(void) {}

static int wild(const char *m, const char *s)
{
  for (; *m; m++, s++) {
    if (*m == '*') {
      for (m++; ; s++) { if (wild(m, s)) return 1; if (!*s) return 0; }
    }
    if (!*s || (*m != '?' && toupper((unsigned char)*m) != toupper((unsigned char)*s))) return 0;
  }
  return !*s;
}

int8_t h_FilesFindMatch(uint8_t *filename, uint8_t *mask, int64_t fuf_flags)
{//mask: "*.HC*;*.DD*" -- any entry matching the file part
  char m[256], *tok, *ctx;
  const char *base = strrchr((const char *)filename, '/');
  (void)fuf_flags;
  base = base ? base + 1 : (const char *)filename;
  strncpy(m, (const char *)mask, sizeof m - 1);
  m[sizeof m - 1] = 0;
  for (tok = strtok_s(m, ";", &ctx); tok; tok = strtok_s(NULL, ";", &ctx))
    if (wild(tok, base)) return 1;
  return 0;
}

/* The translator turns RegDft's default text into ordinary globals, so there is nothing
   to compile here.  shortcut: high scores are not saved between runs; persist RegWrite
   text to a host file and parse it in RegExe if a game needs that. */
int8_t h_RegDft(uint8_t *path, uint8_t *val, int8_t is_adam_entry) { (void)path; (void)val; (void)is_adam_entry; return 0; }
int64_t h_RegExe(uint8_t *path) { (void)path; return 0; }
int8_t h_RegWrite(uint8_t *path, uint8_t *fmt, int64_t argc, int64_t *argv) { (void)path; (void)fmt; (void)argc; (void)argv; return 0; }

int8_t h_Option(int64_t num, int8_t val)
{
  int8_t old;
  if (num < 0 || num >= 256) return 0;
  old = (opts[num >> 6] >> (num & 63)) & 1;
  if (val) opts[num >> 6] |= 1LL << (num & 63);
  else opts[num >> 6] &= ~(1LL << (num & 63));
  return old;
}

/* mkicons.py's IconGen.HC: save a DC as <TOS_ICON_DIR>/<name>.raw (int32 w, h, then 8-bit pixels). */
void h_TosIconSave(c_CDC *dc, uint8_t *name)
{
  char path[260];
  const char *dir = getenv("TOS_ICON_DIR");
  int32_t wh[2] = {(int32_t)dc->m_width, (int32_t)dc->m_height};
  FILE *f;
  snprintf(path, sizeof path, "%s/%s.raw", dir ? dir : ".", (const char *)name);
  if (!(f = fopen(path, "wb"))) return;
  fwrite(wh, 4, 2, f);
  for (int32_t y = 0; y < wh[1]; y++)
    fwrite(dc->m_body + y * dc->m_width_internal, 1, (size_t)wh[0], f);
  fclose(f);
}
