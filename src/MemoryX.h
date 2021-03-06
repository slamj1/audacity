#ifndef __AUDACITY_MEMORY_X_H__
#define __AUDACITY_MEMORY_X_H__

// C++ standard header <memory> with a few extensions
#include <memory>
#include <cstdlib> // Needed for free.
#ifndef safenew
#define safenew new
#endif

// Conditional compilation switch indicating whether to rely on
// std:: containers knowing about rvalue references
#undef __AUDACITY_OLD_STD__


#include <functional>

#if !(_MSC_VER >= 1800 || __cplusplus >= 201402L)
/* replicate the very useful C++14 make_unique for those build environments
that don't implement it yet.
typical useage:
auto p = std::make_unique<Myclass>(ctorArg1, ctorArg2, ... ctorArgN);
p->DoSomething();
auto q = std::make_unique<Myclass[]>(count);
q[0].DoSomethingElse();

The first hides naked NEW and DELETE from the source code.
The second hides NEW[] and DELETE[].  Both of course ensure destruction if
you don't use something like std::move(p) or q.release().  Both expressions require
that you identify the type only once, which is brief and less error prone.

(Whereas this omission of [] might invite a runtime error:
std::unique_ptr<Myclass> q { safenew Myclass[count] }; )

Some C++11 tricks needed here are (1) variadic argument lists and
(2) making the compile-time dispatch work correctly.  You can't have
a partially specialized template function, but you get the effect of that
by other metaprogramming means.
*/

namespace std {
   // For overloading resolution
   template <typename X> struct __make_unique_result {
      using scalar_case = unique_ptr<X>;
   };

   // Partial specialization of the struct for array case
   template <typename X> struct __make_unique_result<X[]> {
      using array_case = unique_ptr<X[]>;
      using element = X;
   };

   // Now the scalar version of unique_ptr
   template<typename X, typename... Args> inline
      typename __make_unique_result<X>::scalar_case
      make_unique(Args&&... args)
   {
      return typename __make_unique_result<X>::scalar_case
      { safenew X(forward<Args>(args)...) };
   }

   // Now the array version of unique_ptr
   // The compile-time dispatch trick is that the non-existence
   // of the scalar_case type makes the above overload
   // unavailable when the template parameter is explicit
   template<typename X> inline
      typename __make_unique_result<X>::array_case
      make_unique(size_t count)
   {
      return typename __make_unique_result<X>::array_case
      { safenew typename __make_unique_result<X>::element[count] };
   }
}
#endif

/*
 * ArrayOf<X>
 * Not to be confused with std::array (which takes a fixed size) or std::vector
 * This maintains a pointer allocated by NEW X[].  It's cheap: only one pointer,
 * with no size and capacity information for resizing as for vector, and if X is
 * a built-in numeric or pointer type, by default there is no zero filling at
 * allocation time.
 */

template<typename X>
class ArrayOf : public std::unique_ptr<X[]>
{
public:
   ArrayOf() {}

   template<typename Integral>
   explicit ArrayOf(Integral count, bool initialize = false)
   {
      static_assert(std::is_unsigned<Integral>::value, "Unsigned arguments only");
      reinit(count, initialize);
   }

   //ArrayOf(const ArrayOf&) PROHIBITED;
   ArrayOf(const ArrayOf&) = delete;
   ArrayOf(ArrayOf&& that)
      : std::unique_ptr < X[] >
         (std::move((std::unique_ptr < X[] >&)(that)))
   {
   }
   ArrayOf& operator= (ArrayOf &&that)
   {
      std::unique_ptr<X[]>::operator=(std::move(that));
      return *this;
   }
   ArrayOf& operator= (std::unique_ptr<X[]> &&that)
   {
      std::unique_ptr<X[]>::operator=(std::move(that));
      return *this;
   }

   template< typename Integral >
   void reinit(Integral count,
               bool initialize = false)
   {
      static_assert(std::is_unsigned<Integral>::value, "Unsigned arguments only");
      if (initialize)
         // Initialize elements (usually, to zero for a numerical type)
         std::unique_ptr<X[]>::reset(safenew X[count]{});
      else
         // Avoid the slight initialization overhead
         std::unique_ptr<X[]>::reset(safenew X[count]);
   }
};

/*
 * ArraysOf<X>
 * This simplifies arrays of arrays, each array separately allocated with NEW[]
 * But it might be better to use std::Array<ArrayOf<X>, N> for some small constant N
 * Or use just one array when sub-arrays have a common size and are not large.
 */
template<typename X>
class ArraysOf : public ArrayOf<ArrayOf<X>>
{
public:
   ArraysOf() {}

   template<typename Integral>
   explicit ArraysOf(Integral N)
      : ArrayOf<ArrayOf<X>>( N )
   {}

   template<typename Integral1, typename Integral2 >
   ArraysOf(Integral1 N, Integral2 M, bool initialize = false)
   : ArrayOf<ArrayOf<X>>( N )
   {
      static_assert(std::is_unsigned<Integral1>::value, "Unsigned arguments only");
      static_assert(std::is_unsigned<Integral2>::value, "Unsigned arguments only");
      for (size_t ii = 0; ii < N; ++ii)
         (*this)[ii] = ArrayOf<X>{ M, initialize };
   }

   //ArraysOf(const ArraysOf&) PROHIBITED;
   ArraysOf(const ArraysOf&) =delete;
   ArraysOf& operator= (ArraysOf&& that)
   {
      ArrayOf<ArrayOf<X>>::operator=(std::move(that));
      return *this;
   }

   template< typename Integral >
   void reinit(Integral count)
   {
      ArrayOf<ArrayOf<X>>::reinit( count );
   }

   template< typename Integral >
   void reinit(Integral count, bool initialize)
   {
      ArrayOf<ArrayOf<X>>::reinit( count, initialize );
   }

   template<typename Integral1, typename Integral2 >
   void reinit(Integral1 countN, Integral2 countM, bool initialize = false)
   {
      static_assert(std::is_unsigned<Integral1>::value, "Unsigned arguments only");
      static_assert(std::is_unsigned<Integral2>::value, "Unsigned arguments only");
      reinit(countN, false);
      for (size_t ii = 0; ii < countN; ++ii)
         (*this)[ii].reinit(countM, initialize);
   }
};

/*
 * template class Maybe<X>
 * Can be used for monomorphic objects that are stack-allocable, but only conditionally constructed.
 * You might also use it as a member.
 * Initialize with create(), then use like a smart pointer,
 * with *, ->, get(), reset(), or in if()
 */

// Placement-NEW is used below, and that does not cooperate with the DEBUG_NEW for Visual Studio
#ifdef _DEBUG
#ifdef _MSC_VER
#undef new
#endif
#endif

template<typename X>
class Maybe {
public:

   // Construct as NULL
   Maybe() {}

   // Supply the copy and move, so you might use this as a class member too
   Maybe(const Maybe &that)
   {
      if (that.get())
         create(*that);
   }

   Maybe& operator= (const Maybe &that)
   {
      if (this != &that) {
         if (that.get())
            create(*that);
         else
            reset();
      }
      return *this;
   }

   Maybe(Maybe &&that)
   {
      if (that.get())
         create(::std::move(*that));
   }

   Maybe& operator= (Maybe &&that)
   {
      if (this != &that) {
         if (that.get())
            create(::std::move(*that));
         else
            reset();
      }
      return *this;
   }

   // Make an object in the buffer, passing constructor arguments,
   // but destroying any previous object first
   // Note that if constructor throws, we remain in a consistent
   // NULL state -- giving exception safety but only weakly
   // (previous value was lost if present)
   template<typename... Args>
   void create(Args&&... args)
   {
      // Lose any old value
      reset();
      // Create NEW value
      pp = safenew(address()) X(std::forward<Args>(args)...);
   }

   // Destroy any object that was built in it
   ~Maybe()
   {
      reset();
   }

   // Pointer-like operators

   // Dereference, with the usual bad consequences if NULL
   X &operator* () const
   {
      return *pp;
   }

   X *operator-> () const
   {
      return pp;
   }

   X* get() const
   {
      return pp;
   }

   void reset()
   {
      if (pp)
         pp->~X(), pp = nullptr;
   }

   // So you can say if(ptr)
   explicit operator bool() const
   {
      return pp != nullptr;
   }

private:
   X* address()
   {
      return reinterpret_cast<X*>(&storage);
   }

   // Data
#if 0
   typename ::std::aligned_storage<
      sizeof(X)
      // , alignof(X) // Not here yet in all compilers
   >::type storage{};
#else
   union {
      double d;
      char storage[sizeof(X)];
   };
#endif
   X* pp{ nullptr };
};

// Restore definition of debug new
#ifdef _DEBUG
#ifdef _MSC_VER
#undef THIS_FILE
static char*THIS_FILE = __FILE__;
#define new new(_NORMAL_BLOCK, THIS_FILE, __LINE__)
#endif
#endif

// Frequently, we need to use a vector or list of unique_ptr if we can, but default
// to shared_ptr if we can't (because containers know how to copy elements only,
// not move them).
#ifdef __AUDACITY_OLD_STD__
template<typename T> using movable_ptr = std::shared_ptr<T>;
template<typename T, typename Deleter> using movable_ptr_with_deleter_base = std::shared_ptr<T>;
#else
template<typename T> using movable_ptr = std::unique_ptr<T>;
template<typename T, typename Deleter> using movable_ptr_with_deleter_base = std::unique_ptr<T, Deleter>;
#endif

template<typename T, typename... Args>
inline movable_ptr<T> make_movable(Args&&... args)
{
   return std::
#ifdef __AUDACITY_OLD_STD__
      make_shared
#else
      make_unique
#endif
      <T>(std::forward<Args>(args)...);
}

template<typename T, typename Deleter> class movable_ptr_with_deleter
   : public movable_ptr_with_deleter_base < T, Deleter >
{
public:
   // Do not expose a constructor that takes only a pointer without deleter
   // That is important when implemented with shared_ptr
   movable_ptr_with_deleter() {};
   movable_ptr_with_deleter(T* p, const Deleter &d)
      : movable_ptr_with_deleter_base<T, Deleter>( p, d ) {}

#ifdef __AUDACITY_OLD_STD__

   // copy
   movable_ptr_with_deleter(const movable_ptr_with_deleter &that)
      : movable_ptr_with_deleter_base < T, Deleter > ( that )
   {
   }

   movable_ptr_with_deleter &operator= (const movable_ptr_with_deleter& that)
   {
      if (this != &that) {
         ((movable_ptr_with_deleter_base<T, Deleter>&)(*this)) =
            that;
      }
      return *this;
   }

#else

   // move
   movable_ptr_with_deleter(movable_ptr_with_deleter &&that)
      : movable_ptr_with_deleter_base < T, Deleter > ( std::move(that) )
   {
   }

   movable_ptr_with_deleter &operator= (movable_ptr_with_deleter&& that)
   {
      if (this != &that) {
         ((movable_ptr_with_deleter_base<T, Deleter>&)(*this)) =
         std::move(that);
      }
      return *this;
   }

#endif
};

template<typename T, typename Deleter, typename... Args>
inline movable_ptr_with_deleter<T, Deleter>
make_movable_with_deleter(const Deleter &d, Args&&... args)
{
   return movable_ptr_with_deleter<T, Deleter>(safenew T(std::forward<Args>(args)...), d);
}

/*
 * A deleter for pointers obtained with malloc
 */
struct freer { void operator() (void *p) const { free(p); } };

/*
 * A useful alias for holding the result of malloc
 */
template< typename T >
using MallocPtr = std::unique_ptr< T, freer >;

/*
 * A useful alias for holding the result of strup and similar
 */
template <typename Character = char>
using MallocString = std::unique_ptr< Character[], freer >;

/*
 * A deleter class to supply the second template parameter of unique_ptr for
 * classes like wxWindow that should be sent a message called Destroy rather
 * than be deleted directly
 */
template <typename T>
struct Destroyer {
   void operator () (T *p) const { if (p) p->Destroy(); }
};

/*
 * a convenience for using Destroyer
 */
template <typename T>
using Destroy_ptr = std::unique_ptr<T, Destroyer<T>>;

/*
 * "finally" as in The C++ Programming Language, 4th ed., p. 358
 * Useful for defining ad-hoc RAII actions.
 * typical usage:
 * auto cleanup = finally([&]{ ... code; ... });
 */

// Construct this from any copyable function object, such as a lambda
template <typename F>
struct Final_action {
   Final_action(F f) : clean( f ) {}
   ~Final_action() { clean(); }
   F clean;
};

// Function template with type deduction lets you construct Final_action
// without typing any angle brackets
template <typename F>
Final_action<F> finally (F f)
{
   return Final_action<F>(f);
}

#include <wx/utils.h> // for wxMin, wxMax
#include <algorithm>

/*
 * Set a variable temporarily in a scope
 */
template< typename T >
struct RestoreValue {
   T oldValue;
   void operator () ( T *p ) const { if (p) *p = oldValue; }
};

template< typename T >
class ValueRestorer : public std::unique_ptr< T, RestoreValue<T> >
{
   using std::unique_ptr< T, RestoreValue<T> >::reset; // make private
   // But release() remains public and can be useful to commit a changed value
public:
   explicit ValueRestorer( T &var )
      : std::unique_ptr< T, RestoreValue<T> >( &var, { var } )
   {}
   explicit ValueRestorer( T &var, const T& newValue )
      : std::unique_ptr< T, RestoreValue<T> >( &var, { var } )
   { var = newValue; }
   ValueRestorer(ValueRestorer &&that)
      : std::unique_ptr < T, RestoreValue<T> > ( std::move(that) ) {};
   ValueRestorer & operator= (ValueRestorer &&that)
   {
      if (this != &that)
         std::unique_ptr < T, RestoreValue<T> >::operator=(std::move(that));
      return *this;
   }
};

// inline functions provide convenient parameter type deduction
template< typename T >
ValueRestorer< T > valueRestorer( T& var )
{ return ValueRestorer< T >{ var }; }

template< typename T >
ValueRestorer< T > valueRestorer( T& var, const T& newValue )
{ return ValueRestorer< T >{ var, newValue }; }

/*
 * A convenience for use with range-for
 */
template <typename Iterator>
struct IteratorRange : public std::pair<Iterator, Iterator> {
   using iterator = Iterator;
   using reverse_iterator = std::reverse_iterator<Iterator>;

   IteratorRange (const Iterator &a, const Iterator &b)
   : std::pair<Iterator, Iterator> ( a, b ) {}

   IteratorRange (Iterator &&a, Iterator &&b)
   : std::pair<Iterator, Iterator> ( std::move(a), std::move(b) ) {}

   IteratorRange< reverse_iterator > reversal () const
   { return { this->rbegin(), this->rend() }; }

   Iterator begin() const { return this->first; }
   Iterator end() const { return this->second; }

   reverse_iterator rbegin() const { return reverse_iterator{ this->second }; }
   reverse_iterator rend() const { return reverse_iterator{ this->first }; }

   bool empty() const { return this->begin() == this->end(); }
   explicit operator bool () const { return !this->empty(); }
   size_t size() const { return std::distance(this->begin(), this->end()); }

   template <typename T> iterator find(const T &t) const
   { return std::find(this->begin(), this->end(), t); }

   template <typename T> long index(const T &t) const
   {
      auto iter = this->find(t);
      if (iter == this->end())
         return -1;
      return std::distance(this->begin(), iter);
   }

   template <typename T> bool contains(const T &t) const
   { return this->end() != this->find(t); }

   template <typename F> iterator find_if(const F &f) const
   { return std::find_if(this->begin(), this->end(), f); }

   template <typename F> long index_if(const F &f) const
   {
      auto iter = this->find_if(f);
      if (iter == this->end())
         return -1;
      return std::distance(this->begin(), iter);
   }

   // to do: use std::all_of, any_of, none_of when available on all platforms
   template <typename F> bool all_of(const F &f) const
   {
      auto notF =
         [&](typename std::iterator_traits<Iterator>::reference v)
            { return !f(v); };
      return !this->any_of( notF );
   }

   template <typename F> bool any_of(const F &f) const
   { return this->end() != this->find_if(f); }

   template <typename F> bool none_of(const F &f) const
   { return !this->any_of(f); }

   template<typename T> struct identity
      { const T&& operator () (T &&v) const { return std::forward(v); } };

   // Like std::accumulate, but the iterators implied, and with another
   // unary operation on the iterator value, pre-composed
   template<
      typename R,
      typename Binary = std::plus< R >,
      typename Unary = identity< decltype( *std::declval<Iterator>() ) >
   >
   R accumulate(
      R init,
      Binary binary_op = {},
      Unary unary_op = {}
   ) const
   {
      R result = init;
      for (auto&& v : *this)
         result = binary_op(result, unary_op(v));
      return result;
   }

   // An overload making it more convenient to use with pointers to member
   // functions
   template<
      typename R,
      typename Binary = std::plus< R >,
      typename R2, typename C
   >
   R accumulate(
      R init,
      Binary binary_op,
      R2 (C :: * pmf) () const
   ) const
   {
      return this->accumulate( init, binary_op, std::mem_fun( pmf ) );
   }

   // Some accumulations frequent enough to be worth abbreviation:
   template<
      typename Unary = identity< decltype( *std::declval<Iterator>() ) >,
      typename R = decltype( std::declval<Unary>()( *std::declval<Iterator>() ) )
   >
   R min( Unary unary_op = {} ) const
   {
      return this->accumulate(
         std::numeric_limits< R >::max(),
         (const R&(*)(const R&, const R&)) std::min,
         unary_op
      );
   }

   template<
      typename R2, typename C,
      typename R = R2
   >
   R min( R2 (C :: * pmf) () const ) const
   {
      return this->min( std::mem_fun( pmf ) );
   }

   template<
      typename Unary = identity< decltype( *std::declval<Iterator>() ) >,
      typename R = decltype( std::declval<Unary>()( *std::declval<Iterator>() ) )
   >
   R max( Unary unary_op = {} ) const
   {
      return this->accumulate(
         -std::numeric_limits< R >::max(),
         // std::numeric_limits< R >::lowest(), // TODO C++11
         (const R&(*)(const R&, const R&)) std::max,
         unary_op
      );
   }

   template<
      typename R2, typename C,
      typename R = R2
   >
   R max( R2 (C :: * pmf) () const ) const
   {
      return this->max( std::mem_fun( pmf ) );
   }

   template<
      typename Unary = identity< decltype( *std::declval<Iterator>() ) >,
      typename R = decltype( std::declval<Unary>()( *std::declval<Iterator>() ) )
   >
   R sum( Unary unary_op = {} ) const
   {
      return this->accumulate(
         R{ 0 },
         std::plus< R >{},
         unary_op
      );
   }

   template<
      typename R2, typename C,
      typename R = R2
   >
   R sum( R2 (C :: * pmf) () const ) const
   {
      return this->sum( std::mem_fun( pmf ) );
   }
};

template< typename Iterator>
IteratorRange< Iterator >
make_iterator_range( const Iterator &i1, const Iterator &i2 )
{
   return { i1, i2 };
}

template< typename Container >
IteratorRange< typename Container::iterator >
make_iterator_range( Container &container )
{
   return { container.begin(), container.end() };
}

template< typename Container >
IteratorRange< typename Container::const_iterator >
make_iterator_range( const Container &container )
{
   return { container.begin(), container.end() };
}

/*
 * Transform an iterator sequence, as another iterator sequence
 */
template <
   typename Result,
   typename Iterator
>
class transform_iterator
   : public std::iterator<
      typename std::iterator_traits<Iterator>::iterator_category,
      const Result
   >
{
   // This takes a function on iterators themselves, not on the
   // dereference of those iterators, in case you ever need the generality.
   using Function = std::function< Result( const Iterator& ) >;

private:
   Iterator mIterator;
   Function mFunction;

public:
   transform_iterator(const Iterator &iterator, const Function &function)
      : mIterator( iterator )
      , mFunction( function )
   {}

   transform_iterator &operator ++ ()
      { ++this->mIterator; return *this; }
   transform_iterator operator ++ (int)
      { auto copy{*this}; ++this->mIterator; return copy; }
   transform_iterator &operator -- ()
      { --this->mIterator; return *this; }
   transform_iterator operator -- (int)
      { auto copy{*this}; --this->mIterator; return copy; }

   typename transform_iterator::reference operator * ()
      { return this->mFunction(this->mIterator); }

   friend inline bool operator == (
      const transform_iterator &a, const transform_iterator &b)
      { return a.mIterator == b.mIterator; }
   friend inline bool operator != (
      const transform_iterator &a, const transform_iterator &b)
      { return !(a == b); }
};

template <
   typename Iterator,
   typename Function
>
transform_iterator<
   decltype( std::declval<Function>() ( std::declval<Iterator>() ) ),
   Iterator
>
make_transform_iterator(const Iterator &iterator, Function function)
{
   return { iterator, function };
}

template < typename Function, typename Iterator > struct value_transformer
{
   // Adapts a function on values to a function on iterators.
   Function function;

   auto operator () (const Iterator &iterator)
      -> decltype( function( *iterator ) ) const
   { return this->function( *iterator ); }
};

template <
   typename Function,
   typename Iterator
>
using value_transform_iterator = transform_iterator<
   decltype( std::declval<Function>()( *std::declval<Iterator>() ) ),
   Iterator
>;

template <
   typename Function,
   typename Iterator
>
value_transform_iterator< Function, Iterator >
make_value_transform_iterator(const Iterator &iterator, Function function)
{
   using NewFunction = value_transformer<Function, Iterator>;
   return { iterator, NewFunction{ function } };
}

#if !wxCHECK_VERSION(3, 1, 0)
// For using std::unordered_map on wxString
namespace std
{
   template<typename T> struct hash;
   template<> struct hash< wxString > {
      size_t operator () (const wxString &str) const // noexcept
      {
         auto stdstr = str.ToStdWstring(); // no allocations, a cheap fetch
         using Hasher = hash< decltype(stdstr) >;
         return Hasher{}( stdstr );
      }
   };
}
#endif

#endif // __AUDACITY_MEMORY_X_H__
