#include <lem.h>
#include <stdint.h>
#include <opus.h>
#include <stdlib.h>

static const char *opus_decoder_mt = "opus_decoder_mt";
static const char *opus_encoder_mt = "opus_encoder_mt";

struct LemOpusDecoder {
  OpusDecoder *dec;
  int channels;
  int samplerate;
};

struct _decoder_async_decode {
  struct lem_async a;
  struct LemOpusDecoder *dec;
  char *data_in;
  int data_in_len;
  int frame_size;
  int decode_fec;
  int16_t *pcm;
  lua_State *T;
  int ret;
};

static void
decoder_decode_reap(struct lem_async *a) {
  struct _decoder_async_decode *job = (struct _decoder_async_decode*) a;
  lua_State *T = job->T;
  
  if (job->ret <= 0) {
    lua_pushnil(T);
    lua_pushinteger(T, job->ret);
    lem_queue(T, 2);
  } else {
    lua_pushlstring(T, (const char*)job->pcm, job->ret*job->dec->channels*sizeof(int16_t));
    lem_queue(T, 1);
  }

  free(job->pcm);
  free(job->data_in);
  free(job);
}

static void
decoder_decode_work(struct lem_async *a) {
  struct _decoder_async_decode *job = (struct _decoder_async_decode*) a;
  int buffer_size = 120/1000.0*job->dec->samplerate*job->dec->channels;

  job->pcm = lem_xmalloc(buffer_size*sizeof(int16_t));
  job->ret = opus_decode(
    job->dec->dec,
    job->data_in,
    job->data_in_len,
    job->pcm,
    buffer_size/job->dec->channels,
    job->decode_fec
  );
}

static int
decoder_decode(lua_State *T) {
  struct LemOpusDecoder *dec = lua_touserdata(T, 1);
  size_t data_len;
  const char *data = lua_tolstring(T, 2, &data_len);
  int decode_fec = lua_tointeger(T, 3);
  
  struct _decoder_async_decode *job = lem_xmalloc(sizeof(*job));
  job->data_in = lem_xmalloc(data_len);
  memcpy(job->data_in, data, data_len);
  job->data_in_len = data_len;
  job->T = T;
  job->decode_fec = decode_fec;
  job->dec = dec;
  lem_async_do(&job->a, decoder_decode_work, decoder_decode_reap);

  lua_settop(T, 3);
  return lua_yield(T, 3);
}


static int
new_decoder(lua_State *L) {
  OpusDecoder *dec;
  
  int samplerate = lua_tointeger(L, 1);
  int channels = lua_tointeger(L, 2);
  
  int err;
  dec = opus_decoder_create(samplerate, channels, &err);
  if (dec == NULL) {
    lua_pushnil(L);
    lua_pushinteger(L, err);
    return 2;
  }

  struct LemOpusDecoder *ldec = lua_newuserdata(L, sizeof(*ldec));
  ldec->dec = dec;
  ldec->samplerate = samplerate;
  ldec->channels = channels;
  luaL_setmetatable(L, opus_decoder_mt);

  return 1;
}

struct LemOpusEncoder {
  OpusEncoder *enc;
  int samplerate;
  int channels;
};

static int
new_encoder(lua_State *L) {
  OpusEncoder *enc;
  
  int samplerate = lua_tointeger(L, 1);
  int channels = lua_tointeger(L, 2);
  int bitrate = lua_tointeger(L, 3);
  int voice_optimization = lua_tointeger(L, 4);
  
  int err;
  enc = opus_encoder_create(samplerate, channels, (voice_optimization ? OPUS_APPLICATION_VOIP : OPUS_APPLICATION_AUDIO), &err);

  if(enc != NULL) {
    if(bitrate <= 0)
      opus_encoder_ctl(enc, OPUS_SET_BITRATE(OPUS_BITRATE_MAX));
    else
      opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
  }
  
  if (enc == NULL) {
    lua_pushnil(L);
    lua_pushinteger(L, err);
    return 2;
  }

  struct LemOpusEncoder *lenc = lua_newuserdata(L, sizeof(*lenc));
  lenc->enc = enc;
  lenc->samplerate = samplerate;
  lenc->channels = channels;
  luaL_setmetatable(L, opus_encoder_mt);

  return 1;
}

struct _encoder_async_encode {
  struct lem_async a;
  struct LemOpusEncoder *enc;
  int16_t *pcm;
  int pcm_len;
  char *data;
  lua_State *T;
  int ret;
};

static void
encoder_encode_reap(struct lem_async *a) {
  struct _encoder_async_encode *job = (struct _encoder_async_encode*) a;
  lua_State *T = job->T;
  
  if (job->ret <= 0) {
    lua_pushnil(T);
    lua_pushinteger(T, job->ret);
    lem_queue(T, 2);
  } else {
    lua_pushlstring(T, (const char*)job->data, job->ret);
    lem_queue(T, 1);
  }

  free(job->data);
  free(job->pcm);
  free(job);
}


static void
encoder_encode_work(struct lem_async *a) {
  struct _encoder_async_encode *job = (struct _encoder_async_encode*) a;
  
  int sample = (job->pcm_len/sizeof(int16_t)) / job->enc->channels;

  int max_size = sample * sizeof(int16_t);

  job->data = lem_xmalloc(max_size);

  int frame_size = job->pcm_len/job->enc->channels/sizeof(int16_t);

  job->ret = opus_encode(
    job->enc->enc,
    (opus_int16*)job->pcm,
    frame_size,
    job->data,
    max_size
  );
}

static int
encoder_encode(lua_State *T) {
  struct LemOpusEncoder *enc = lua_touserdata(T, 1);
  size_t pcm_len;
  const char *pcm = lua_tolstring(T, 2, &pcm_len);
  
  struct _encoder_async_encode *job = lem_xmalloc(sizeof(*job));
  job->pcm = lem_xmalloc(pcm_len);
  memcpy(job->pcm, pcm, pcm_len);
  job->pcm_len = pcm_len;
  job->T = T;
  job->enc = enc;

  lem_async_do(&job->a, encoder_encode_work, encoder_encode_reap);

  lua_settop(T, 2);
  return lua_yield(T, 2);
}

int luaopen_lem_opus_core(lua_State *L) {
  luaL_newmetatable(L, opus_decoder_mt);

  lua_pushcfunction(L, decoder_decode);
  lua_setfield(L, -2, "decode");

  lua_setfield(L, -1, "__index");

  luaL_newmetatable(L, opus_encoder_mt);

  lua_pushcfunction(L, encoder_encode);
  lua_setfield(L, -2, "encode");

  lua_setfield(L, -1, "__index");


  lua_newtable(L);
  lua_pushcfunction(L, new_decoder);
  lua_setfield(L, -2, "new_decoder");

  lua_pushcfunction(L, new_encoder);
  lua_setfield(L, -2, "new_encoder");

  return 1;
}
