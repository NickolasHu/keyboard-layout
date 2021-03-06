#include "keyboard-layout-manager.h"

#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XKBrules.h>

void KeyboardLayoutManager::Init(v8::Handle<v8::Object> exports, v8::Handle<v8::Object> module) {
  Nan::HandleScope scope;
  v8::Local<v8::FunctionTemplate> newTemplate = Nan::New<v8::FunctionTemplate>(KeyboardLayoutManager::New);
  newTemplate->SetClassName(Nan::New<v8::String>("KeyboardLayoutManager").ToLocalChecked());
  newTemplate->InstanceTemplate()->SetInternalFieldCount(1);
  v8::Local<v8::ObjectTemplate> proto = newTemplate->PrototypeTemplate();
  Nan::SetMethod(proto, "getCurrentKeyboardLayout", KeyboardLayoutManager::GetCurrentKeyboardLayout);
  Nan::SetMethod(proto, "getCurrentKeyboardLanguage", KeyboardLayoutManager::GetCurrentKeyboardLayout); // NB:  Intentionally mapped to same stub
  Nan::SetMethod(proto, "getInstalledKeyboardLanguages", KeyboardLayoutManager::GetInstalledKeyboardLanguages);
  Nan::SetMethod(proto, "getCurrentKeymap", KeyboardLayoutManager::GetCurrentKeymap);
  module->Set(Nan::New("exports").ToLocalChecked(), newTemplate->GetFunction());
}

NODE_MODULE(keyboard_layout_manager, KeyboardLayoutManager::Init)

NAN_METHOD(KeyboardLayoutManager::New) {
  Nan::HandleScope scope;

  v8::Local<v8::Function> callbackHandle = info[0].As<v8::Function>();
  Nan::Callback *callback = new Nan::Callback(callbackHandle);

  KeyboardLayoutManager *manager = new KeyboardLayoutManager(callback);
  manager->Wrap(info.This());
  return;
}

KeyboardLayoutManager::KeyboardLayoutManager(Nan::Callback *callback) : callback(callback) {
  xDisplay = XOpenDisplay("");
  if (!xDisplay) {
    Nan::ThrowError("Could not connect to X display.");
  }
}

KeyboardLayoutManager::~KeyboardLayoutManager() {
  XCloseDisplay(xDisplay);
  delete callback;
};

void KeyboardLayoutManager::HandleKeyboardLayoutChanged() {
}

NAN_METHOD(KeyboardLayoutManager::GetCurrentKeyboardLayout) {
  Nan::HandleScope scope;
  KeyboardLayoutManager* manager = Nan::ObjectWrap::Unwrap<KeyboardLayoutManager>(info.Holder());
  v8::Handle<v8::Value> result;

  XkbRF_VarDefsRec vdr;
  char *tmp = NULL;
  if (XkbRF_GetNamesProp(manager->xDisplay, &tmp, &vdr) && vdr.layout && vdr.variant) {
    result = Nan::New<v8::String>(std::string(vdr.layout) + "," + std::string(vdr.variant)).ToLocalChecked();
  } else {
    result = Nan::Null();
  }

  info.GetReturnValue().Set(result);

  return;
}

NAN_METHOD(KeyboardLayoutManager::GetInstalledKeyboardLanguages) {
  Nan::HandleScope scope;
  return;
}

struct KeycodeMapEntry {
  uint xkbKeycode;
  const char *dom3Code;
};

#define USB_KEYMAP_DECLARATION static const KeycodeMapEntry keyCodeMap[] =
#define USB_KEYMAP(usb, evdev, xkb, win, mac, code, id) {xkb, code}

#include "keycode_converter_data.inc"

v8::Local<v8::Value> CharacterForNativeCode(XKeyEvent *keyEvent, uint xkbKeycode, uint state) {
  keyEvent->keycode = xkbKeycode;
  keyEvent->state = state;

  char characters[2];
  int count = XLookupString(keyEvent, characters, 2, NULL, NULL);

  if (count > 0 && !std::iscntrl(characters[0])) {
    return Nan::New<v8::String>(characters, count).ToLocalChecked();
  } else {
    return Nan::Null();
  }
}

NAN_METHOD(KeyboardLayoutManager::GetCurrentKeymap) {
  v8::Handle<v8::Object> result = Nan::New<v8::Object>();
  KeyboardLayoutManager* manager = Nan::ObjectWrap::Unwrap<KeyboardLayoutManager>(info.Holder());
  v8::Local<v8::String> unmodifiedKey = Nan::New("unmodified").ToLocalChecked();
  v8::Local<v8::String> withShiftKey = Nan::New("withShift").ToLocalChecked();

  // Clear cached keymap
  XMappingEvent eventMap = {MappingNotify, 0, false, manager->xDisplay, 0, MappingKeyboard, 0, 0};
  XRefreshKeyboardMapping(&eventMap);

  // Set up an event to reuse across CharacterForNativeCode calls
  XEvent event;
  memset(&event, 0, sizeof(XEvent));
  XKeyEvent* keyEvent = &event.xkey;
  keyEvent->display = manager->xDisplay;
  keyEvent->type = KeyPress;

  size_t keyCodeMapSize = sizeof(keyCodeMap) / sizeof(keyCodeMap[0]);
  for (size_t i = 0; i < keyCodeMapSize; i++) {
    const char *dom3Code = keyCodeMap[i].dom3Code;
    uint xkbKeycode = keyCodeMap[i].xkbKeycode;

    if (dom3Code && xkbKeycode > 0x0000) {
      v8::Local<v8::String> dom3CodeKey = Nan::New(dom3Code).ToLocalChecked();
      v8::Local<v8::Value> unmodified = CharacterForNativeCode(keyEvent, xkbKeycode, 0);
      v8::Local<v8::Value> withShift = CharacterForNativeCode(keyEvent, xkbKeycode, ShiftMask);

      if (unmodified->IsString() || withShift->IsString()) {
        v8::Local<v8::Object> entry = Nan::New<v8::Object>();
        entry->Set(unmodifiedKey, unmodified);
        entry->Set(withShiftKey, withShift);
        result->Set(dom3CodeKey, entry);
      }
    }
  }

  info.GetReturnValue().Set(result);
}
