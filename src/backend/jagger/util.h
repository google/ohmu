

template <typename Type>
struct RangeAdaptor {
  RangeAdaptor(Type* data, size_t size) : _begin(data), _end(data + size) {}
  RangeAdaptor(Type* begin, Type* end) : _begin(begin), _end(end) {}
  Type* begin() const { return _begin; }
  Type* end() const { return _end; }

 private:
  Type* _begin;
  Type* _end;
};

template <typename Type>
RangeAdaptor<Type> AdaptRange(Type* data, size_t size) {
  return RangeAdaptor<Type>(data, size);
}

template <typename Type>
RangeAdaptor<Type> AdaptRange(Type* begin, Type* end) {
  return RangeAdaptor<Type>(begin, end);
}
