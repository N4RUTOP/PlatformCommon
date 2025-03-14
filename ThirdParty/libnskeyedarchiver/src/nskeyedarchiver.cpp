#include "nskeyedarchiver/nskeyedarchiver.hpp"

#include "nskeyedarchiver/common.hpp"
#include "nskeyedarchiver/scope.hpp"

using namespace nskeyedarchiver;

#ifdef _MSC_VER
#pragma warning(disable : 4244)
#endif

NSKeyedArchiver::NSKeyedArchiver(NSClassManager* class_manager)
    : class_manager_(class_manager), output_format_(NSKeyedArchiver::Xml) {
  null_object_ = plist_new_string(kNSKeyedArchiveNullObjectReferenceName);
  objects_.emplace_back(null_object_);
  containers_.push(EncodingContext());
}

NSKeyedArchiver::~NSKeyedArchiver() {
  // free each object(`plist_t`) of the array, include the `null_object_·;
  // for (plist_t object : objects_) {
  //  plist_free(object);
  //}
  plist_free(plist_);
}

// static
bool NSKeyedArchiver::ArchivedData(const KAValue& object, char** data, size_t* size,
                                   NSKeyedArchiver::OutputFormat output_format) {
  NSClassManager& class_manager = NSClassManager::GetInstance();
  NSKeyedArchiver archiver(&class_manager);
  archiver.SetOutputFormat(output_format);
  archiver.EncodeObject(object, kNSKeyedArchiveRootObjectKey);
  if (!archiver.HasError()) {  // OK
    archiver.GetEncodedData(data, size);
    return true;
  }
  NSKEYEDARCHIVER_LOG_ERROR("can not archive data, error: %s.\n", archiver.Error().c_str());
  return false;
}

void NSKeyedArchiver::EncodeObject(const KAValue& object, const std::string& key) {
  ObjectRef object_ref = EncodeObject(object);
  if (object_ref != nullptr) {
    SetObjectInCurrentEncodingContext(object_ref, key, !key.empty());
  } else {
    NSKEYEDARCHIVER_LOG_ERROR("can not encode object for key: %s.\n", key.c_str());
  }
}

void NSKeyedArchiver::EncodePrimitive(const KAValue& object, const std::string& key) {
  ObjectRef object_ref = EncodePrimitive(object);
  if (object_ref != nullptr) {
    SetObjectInCurrentEncodingContext(object_ref, key, !key.empty());
  } else {
    NSKEYEDARCHIVER_LOG_ERROR("can not encode primitive for key: %s.\n", key.c_str());
  }
}

void NSKeyedArchiver::EncodeArrayOfObjects(const std::vector<KAValue>& array,
                                           const std::string& key) {
  plist_t object_refs = plist_new_array();
  for (const KAValue& object : array) {
    plist_array_append_item(object_refs, EncodeObject(object));
  }
  EncodeValue(object_refs, key);
}

void NSKeyedArchiver::EncodeArrayOfObjectRefs(NSKeyedArchiver::ObjectRefArray array,
                                              const std::string& key) {
  plist_t object_refs = plist_new_array();
  for (const KAValue& object : array) {
    plist_array_append_item(object_refs, EncodeObject(object));
  }
  EncodeValue(object_refs, key);
}

void NSKeyedArchiver::EncodeValue(ObjectRef plist, const std::string& key) {
  SetObjectInCurrentEncodingContext(plist, key);
}

NSKeyedArchiver::ObjectRef NSKeyedArchiver::EncodeObject(const KAValue& object) {
  NSKeyedArchiverUID object_uid = GetReferencedObjectUid(object);
  bool have_visited = object_uid.IsValid() || object.IsNull();  // always have a null reference
  NSKEYEDARCHIVER_LOG_DEBUG("object=%s, have_visited=%d.\n", object.ToJson().c_str(), have_visited);
  if (!object_uid.IsValid()) {
    // first time to visit this object
    object_uid = ReferenceObject(object);
  }  // else this object has already been referenced
  if (!have_visited) {
    plist_t encoding_object = nullptr;
    if (IsContainer(object)) {
      NSKEYEDARCHIVER_LOG_DEBUG("Is Container\n");

      const NSClass clazz = GetNSClass(object);
      NSClassManager::Serializer& serializer = FindClassSerializer(clazz);

      PushEncodingContext(EncodingContext());
      serializer(this, clazz, object);

      NSKeyedArchiverUID class_uid = ReferenceClass(clazz);
      SetObjectInCurrentEncodingContext(class_uid.AsRef(), "$class", false);
      const EncodingContext& ctx = CurrentEncodingContext();
      encoding_object = ctx.dict;
      PopEncodingContext();
    } else {
      NSKEYEDARCHIVER_LOG_DEBUG("Is Primitive\n");
      encoding_object = EncodePrimitive(object);  // TODO: do we need wrap it with a reference?
    }

    // repleace the placeholder with the object just encoded
    SetObject(object_uid, encoding_object);
  } else {
    NSKEYEDARCHIVER_LOG_DEBUG("this object has been visited. object=%s\n", object.ToJson().c_str());
  }

  // NOTE: even two uid are equal, we always create a new UID plist object as the reference of
  // object because we don't want to have circularly referenced plist objects in the plist tree
  // which libplist can not handle this situcation when freeing them.
  return object_uid.AsRef();
}

plist_t NSKeyedArchiver::EncodePrimitive(const KAValue& object) {
  switch (object.GetDataType()) {
    case KAValue::Null:
      return null_object_;
    case KAValue::Bool:
      return plist_new_bool(object.ToBool());
    case KAValue::Integer:
      return plist_new_uint(object.ToInteger());
    case KAValue::Double:
      return plist_new_real(object.ToDouble());
    case KAValue::Str:
      return plist_new_string(object.ToStr());  // copy char*
    case KAValue::Raw: {
      const KAValue::RawData* raw = object.ToRaw();
      return plist_new_data(raw->data, raw->size);  // copy char*
    }
    case KAValue::Object:  // fallthrough
    default:
      NSKEYEDARCHIVER_ASSERT(false, "unsupport encode KAValue type as primitive: %d.\n", object.GetDataType());
      return nullptr;
  }
}

NSClassManager::Serializer& NSKeyedArchiver::FindClassSerializer(const NSClass& clazz) const {
  if (!clazz.class_name.empty()) {
    if (class_manager_->HasSerializer(clazz.class_name)) {
      return class_manager_->GetSerializer(clazz.class_name);
    }
  }
  NSKEYEDARCHIVER_LOG_ERROR("Can not find the Serializer for class name: `%s`.\n", clazz.class_name.c_str());
  return class_manager_->GetDefaultSerializer();
}

NSClass NSKeyedArchiver::GetNSClass(const KAValue& object) const {
  if (object.IsObject()) {
    const KAObject* inner_obj = object.ToObject();
    return NSClass{inner_obj->ClassName(), inner_obj->Classes(), {/* classhints */}};
  }
  return NSClass(); // with empty class name
}

bool NSKeyedArchiver::IsContainer(const KAValue& object) const { return object.IsObject(); }

NSKeyedArchiverUID NSKeyedArchiver::ReferenceObject(const KAValue& object) {
  if (object.IsNull()) {
    return NSKeyedArchiverUID(kNSKeyedArchiverNullObjectReferenceUid);
  }

  // use the index of objects array as the reference of this object
  NSKeyedArchiverUID uid = objects_.size();
  obj_uid_map_.Put(object, uid);
  objects_.emplace_back(nullptr);  // this object has not been encoded yet, so we use the
                                   // `nullptr` as a placeholder(index = uid)
  return uid;
}

NSKeyedArchiverUID NSKeyedArchiver::GetReferencedObjectUid(const KAValue& object) {
  return obj_uid_map_.Get(object, NSKeyedArchiverUID());
}

NSKeyedArchiverUID NSKeyedArchiver::ReferenceClass(const NSClass& clazz) {
  NSKeyedArchiverUID uid;
  const std::string& class_name = clazz.class_name;
  auto found = class_uid_map_.find(class_name);
  if (found != class_uid_map_.end()) {
    uid = found->second;
  } else {
    uid = objects_.size();
    class_uid_map_[class_name] = uid;
    objects_.emplace_back(EncodeClass(clazz));
  }
  return uid;
}

plist_t NSKeyedArchiver::EncodeClass(const NSClass& clazz) {
  plist_t dict = plist_new_dict();
  plist_dict_set_item(dict, "$classname", plist_new_string(clazz.class_name.c_str()));

  plist_t classes = plist_new_array();
  for (const auto& clazz : clazz.classes) {
    plist_array_append_item(classes, plist_new_string(clazz.c_str()));
  }
  plist_dict_set_item(dict, "$classes", classes);

  if (clazz.classhints.size() > 0) {
    plist_t classhints = plist_new_array();
    for (const auto& clazz : clazz.classhints) {
      plist_array_append_item(classhints, plist_new_string(clazz.c_str()));
    }
    plist_dict_set_item(dict, "$classhints", classhints);
  }

  return dict;
}

void NSKeyedArchiver::SetObject(NSKeyedArchiverUID uid, plist_t encoding_object) {
  objects_[uid.Value()] = encoding_object;
}

EncodingContext& NSKeyedArchiver::CurrentEncodingContext() {
  NSKEYEDARCHIVER_ASSERT(!containers_.empty(), "the containers_ can not be empty.\n");
  return containers_.top();
}

int NSKeyedArchiver::CurrentEncodingContextDepth() const { return (int)containers_.size(); }

void NSKeyedArchiver::PushEncodingContext(EncodingContext&& decoding_context) {
  containers_.push(std::forward<EncodingContext>(decoding_context));
}

void NSKeyedArchiver::PopEncodingContext() { containers_.pop(); }

void NSKeyedArchiver::SetObjectInCurrentEncodingContext(ObjectRef object_ref,
                                                        const std::string& key, bool escape) {
  std::string encoding_key;
  if (!key.empty()) {
    if (escape) {
      encoding_key = EscapeArchiverKey(key);
    } else {
      encoding_key = key;
    }
  } else {
    encoding_key = NextGenericKey();
  }

  EncodingContext& ctx = CurrentEncodingContext();
  plist_dict_set_item(ctx.dict, encoding_key.c_str(),
                      object_ref);  // ctx.dict[encoding_key] = object_ref;
}

// static
std::string NSKeyedArchiver::EscapeArchiverKey(const std::string& key) {
  if (key.size() > 0 && key.at(0) == '$') {
    return "$" + key;
  } else {
    return key;
  }
}

std::string NSKeyedArchiver::NextGenericKey() {
  EncodingContext& ctx = CurrentEncodingContext();
  std::string key = "$" + std::to_string(ctx.generic_key);
  ctx.generic_key = ctx.generic_key + 1;
  return key;
}

void NSKeyedArchiver::GetEncodedData(char** data, size_t* size) {
  if (plist_ == nullptr) {
    plist_ = FinishEncoding();
  }

  if (output_format_ == OutputFormat::Xml) {
    plist_to_xml(plist_, data, (uint32_t*)size);
  } else if (output_format_ == OutputFormat::Binary) {
    plist_to_bin(plist_, data, (uint32_t*)size);
  } else {
    NSKEYEDARCHIVER_ASSERT(false, "unsupport output format: %d.\n", output_format_);
  }
}

plist_t NSKeyedArchiver::FinishEncoding() {
  plist_t plist = plist_new_dict();

  plist_dict_set_item(plist, "$version", plist_new_uint(kNSKeyedArchivePlistVersion));
  plist_dict_set_item(plist, "$archiver", plist_new_string(kNSKeyedArchiveName));

  plist_t top = plist_new_dict();
  NSKEYEDARCHIVER_ASSERT(containers_.size() == 1, "the stack has more the one item.\n");
  EncodingContext& ctx = CurrentEncodingContext();
  /*
  for (auto kv : ctx.dict) {
    plist_t uid = plist_new_uid(kv.second);
    plist_dict_set_item(top, kv.first.c_str(), uid);
  }
  */
  plist_dict_set_item(plist, "$top", ctx.dict);

  plist_t objects = plist_new_array();
  for (plist_t object : objects_) {
    plist_array_append_item(objects, object);
  }
  plist_dict_set_item(plist, "$objects", objects);

  return plist;
}
