// Copyright (c) 2026, Matthew Bentley (mattreecebentley@gmail.com) www.plflib.org

// zLib license (https://www.zlib.net/zlib_license.html):
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// 	claim that you wrote the original software. If you use this software
// 	in a product, an acknowledgement in the product documentation would be
// 	appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// 	misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.


#ifndef PLF_STACK_H
#define PLF_STACK_H

#ifndef PLF_COMPILER_DEFINES
	#define PLF_STACK_DEFINES // ie. No encapsulating unit/class has previously defined the compiler feature macros in plf_tools.h below, so allow this header to undefine them at it's end.
#endif

#define PLF_INCLUDE_UNINITIALIZED_TOOLS
#define PLF_INCLUDE_TOOLS
#include "plf_tools.h"


#include <cstring> // memset, memcpy
#include <cassert> // assert
#include <limits>  // std::numeric_limits
#include <stdexcept> // std::length_error
#include <utility> // std::move, std::swap
#include <algorithm> // std::lexicographical_compare, std::lexicographical_compare_three_way (C++20)

#ifdef PLF_CPP20_SUPPORT
	#include <compare> // std::compare_three_way, std::three_way_comparable, std::weak_ordering
	#include <concepts> // std::convertible_to
#endif


#ifdef PLF_TYPE_TRAITS_SUPPORT
	#include <cstddef> // offsetof, used in blank()
	#include <type_traits> // std::is_trivially_destructible
#endif


namespace plf
{


template <class element_type, class allocator_type = std::allocator<element_type> > class stack : private allocator_type // Empty base class optimisation - inheriting allocator functions
{
public:
	// Standard container typedefs:
	typedef element_type														value_type;

	#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
		typedef typename std::allocator_traits<allocator_type>::size_type		size_type;
		typedef element_type &													reference;
		typedef const element_type &											const_reference;
		typedef typename std::allocator_traits<allocator_type>::pointer 		pointer;
		typedef typename std::allocator_traits<allocator_type>::const_pointer	const_pointer;
	#else
		typedef typename allocator_type::size_type			size_type;
		typedef typename allocator_type::reference			reference;
		typedef typename allocator_type::const_reference	const_reference;
		typedef typename allocator_type::pointer			pointer;
		typedef typename allocator_type::const_pointer		const_pointer;
	#endif

private:
	struct group; // Forward declaration for typedefs below

	#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
		typedef typename std::allocator_traits<allocator_type>::template rebind_alloc<group> group_allocator_type;
		typedef typename std::allocator_traits<group_allocator_type>::pointer				group_pointer_type;
		typedef typename std::allocator_traits<allocator_type>::pointer 					element_pointer_type;
	#else
		typedef typename allocator_type::template rebind<group>::other	group_allocator_type;
		typedef typename group_allocator_type::pointer					group_pointer_type;
		typedef typename allocator_type::pointer						element_pointer_type;
	#endif


	struct group : private allocator_type
	{
		const element_pointer_type		elements;
		group_pointer_type				next_group, previous_group;
		const element_pointer_type		end; // One-past the back element


		#ifdef PLF_VARIADICS_SUPPORT
			PLF_CONSTFUNC group(const allocator_type &alloc, const size_type elements_per_group, const group_pointer_type previous = NULL):
				allocator_type(alloc),
				elements(PLF_ALLOCATE(allocator_type, *this, elements_per_group, (previous == NULL) ? 0 : previous->elements)),
				next_group(NULL),
				previous_group(previous),
				end(elements + elements_per_group)
			{}
		#else
			// This is a hack around the fact that allocator_type::construct only supports copy construction in C++03 and copy elision does not occur on the vast majority of compilers in this circumstance. And to avoid running out of memory (and performance loss) from allocating the same block twice, we're allocating in the copy constructor.
			group(const allocator_type &alloc, const size_type elements_per_group, const group_pointer_type previous = NULL) PLF_NOEXCEPT:
				allocator_type(alloc),
				elements(NULL),
				next_group(reinterpret_cast<group_pointer_type>(elements_per_group)),
				previous_group(previous),
				end(NULL)
			{}


			// Not a real copy constructor ie. actually a move constructor. Only used for allocator.construct in C++03 for reasons stated above:
			group(const group &source):
				allocator_type(source),
				elements(PLF_ALLOCATE(allocator_type, *this, reinterpret_cast<size_type>(source.next_group), (source.previous_group == NULL) ? 0 : source.previous_group->elements)),
				next_group(NULL),
				previous_group(source.previous_group),
				end(elements + reinterpret_cast<size_type>(source.next_group))
			{}
		#endif



		PLF_CONSTFUNC ~group() PLF_NOEXCEPT
		{
			// Null check not necessary (for empty group and copied group as above) as deallocate will do it's own null check.
			PLF_DEALLOCATE(allocator_type, *this, elements, static_cast<size_type>(end - elements));
		}
	};


	group_pointer_type		current_group, first_group; // current group is location of top pointer, first_group is 'front' group, saves performance for ~stack etc
	element_pointer_type		top_element, start_element, end_element; // start_element/end_element cache current_group->end/elements for better performance. top_element is NULL when the stack is empty, even if memory blocks are allocated
	size_type					total_size, total_capacity, min_block_capacity;
	struct ebco_pair : group_allocator_type // Packaging the group allocator with the least-used member variable, for empty-base-class optimization
	{
		size_type max_block_capacity;
		PLF_CONSTFUNC ebco_pair(const size_type max_elements, const allocator_type &alloc) PLF_NOEXCEPT:
			group_allocator_type(alloc),
			max_block_capacity(max_elements)
		{};
	} group_allocator_pair;



	PLF_CONSTFUNC void check_capacities_conformance(const size_type min, const size_type max) const
	{
  		if (min < 2 || min > max || max > (default_max_block_capacity()))
		{
			#ifdef PLF_EXCEPTIONS_SUPPORT
				throw std::length_error("Supplied memory block capacities outside of allowable ranges");
			#else
				std::terminate();
			#endif
		}
	}



public:


	static PLF_CONSTFUNC size_type default_min_block_capacity() PLF_NOEXCEPT
	{
		return (sizeof(element_type) * 8 > (sizeof(stack) + sizeof(group)) * 2) ? 8 : (((sizeof(stack) + sizeof(group)) * 2) / sizeof(element_type)) + 1;
	}



	static PLF_CONSTFUNC size_type default_max_block_capacity() PLF_NOEXCEPT
	{
		return std::numeric_limits<size_type>::max() / 2;
	}



	// Default constructor:
	PLF_CONSTFUNC stack() PLF_NOEXCEPT_ALLOCATOR:
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(default_min_block_capacity()),
		group_allocator_pair(default_max_block_capacity(), *this)
	{}



	// Allocator-extended constructor:
	PLF_CONSTFUNC explicit stack(const allocator_type &alloc):
		allocator_type(alloc),
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(default_min_block_capacity()),
		group_allocator_pair(default_max_block_capacity(), alloc)
	{}



	// Constructor with minimum & maximum group size parameters:
	PLF_CONSTFUNC stack(const size_type min, const size_type max = default_max_block_capacity()):
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(min),
		group_allocator_pair(max, *this)
	{
		check_capacities_conformance(min, max);
	}



	// Allocator-extended constructor with minimum & maximum group size parameters:
	PLF_CONSTFUNC stack(const size_type min, const size_type max, const allocator_type &alloc):
		allocator_type(alloc),
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(min),
		group_allocator_pair(max, alloc)
	{
		check_capacities_conformance(min, max);
	}



private:

	PLF_CONSTFUNC void allocate_new_group(const size_type capacity, const group_pointer_type previous_group)
	{
		previous_group->next_group = PLF_ALLOCATE(group_allocator_type, group_allocator_pair, 1, previous_group);

		#ifdef PLF_EXCEPTIONS_SUPPORT
			try
			{
				#ifdef PLF_VARIADICS_SUPPORT
					PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, previous_group->next_group, static_cast<allocator_type &>(*this), capacity, previous_group);
				#else
					PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, previous_group->next_group, group(static_cast<allocator_type &>(*this), capacity, previous_group));
				#endif
			}
			catch (...)
			{
				PLF_DEALLOCATE(group_allocator_type, group_allocator_pair, previous_group->next_group, 1);
				throw;
			}
		#else
			#ifdef PLF_VARIADICS_SUPPORT
				PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, previous_group->next_group, static_cast<allocator_type &>(*this), capacity, previous_group);
			#else
				PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, previous_group->next_group, group(static_cast<allocator_type &>(*this), capacity, previous_group));
			#endif
		#endif

		total_capacity += capacity;
	}



	PLF_CONSTFUNC void deallocate_group(const group_pointer_type the_group) PLF_NOEXCEPT
	{
		PLF_DESTROY(group_allocator_type, group_allocator_pair, the_group);
		PLF_DEALLOCATE(group_allocator_type, group_allocator_pair, the_group, 1);
	}



	PLF_CONSTFUNC void initialize()
	{
		first_group = current_group = PLF_ALLOCATE(group_allocator_type, group_allocator_pair, 1, 0);

		#ifdef PLF_EXCEPTIONS_SUPPORT
			try
			{
				#ifdef PLF_VARIADICS_SUPPORT
					PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, first_group, static_cast<allocator_type &>(*this), min_block_capacity);
				#else
					PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, first_group, group(static_cast<allocator_type &>(*this), min_block_capacity));
				#endif
			}
			catch (...)
			{
				PLF_DEALLOCATE(group_allocator_type, group_allocator_pair, first_group, 1);
				first_group = current_group = NULL;
				throw;
			}
		#else
			#ifdef PLF_VARIADICS_SUPPORT
				PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, first_group, static_cast<allocator_type &>(*this), min_block_capacity);
			#else
				PLF_CONSTRUCT(group_allocator_type, group_allocator_pair, first_group, group(static_cast<allocator_type &>(*this), min_block_capacity));
			#endif
		#endif

		start_element = top_element = first_group->elements;
		end_element = first_group->end;
		total_capacity = min_block_capacity;
	}



	#ifdef PLF_TYPE_TRAITS_SUPPORT // moving from source path only occurs if type traits are supported in consolidate()
		template <bool is_move = false>
	#endif
	PLF_CONSTFUNC void copy_from_source(const stack &source) // Note: this function is only called on an empty un-initialize()'d stack
	{
		assert(&source != this);

		if (source.total_size == 0) return;

		group_pointer_type current_copy_group = source.first_group;
		const group_pointer_type end_copy_group = source.current_group;

		if (source.total_size <= group_allocator_pair.max_block_capacity) // most common case
		{
			const size_type original_min_block_capacity = min_block_capacity;
			min_block_capacity = source.total_size;
			initialize();
			min_block_capacity = original_min_block_capacity;

			// Copy groups to this stack:
			while (current_copy_group != end_copy_group)
			{
				#ifdef PLF_TYPE_TRAITS_SUPPORT
					if PLF_CONSTEXPR (is_move)
					{
						plf::uninitialized_move(current_copy_group->elements, current_copy_group->end, top_element, static_cast<allocator_type &>(*this));
					}
					else
				#endif
				plf::uninitialized_copy(current_copy_group->elements, current_copy_group->end, top_element, static_cast<allocator_type &>(*this));
				top_element += current_copy_group->end - current_copy_group->elements;
				current_copy_group = current_copy_group->next_group;
			}

			// Handle special case of last group:
			#ifdef PLF_TYPE_TRAITS_SUPPORT
				if PLF_CONSTEXPR (is_move)
				{
					plf::uninitialized_move(source.start_element, source.top_element + 1, top_element, static_cast<allocator_type &>(*this));
				}
				else
			#endif
			plf::uninitialized_copy(source.start_element, source.top_element + 1, top_element, static_cast<allocator_type &>(*this));
			top_element += source.top_element - source.start_element; // This should make top_element == the last "pushed" element, rather than the one past it
			end_element = top_element + 1; // Since we have created a single group where capacity == size, this is correct
			total_size = source.total_size;
		}
		else // uncommon edge case, so not optimising:
		{
			min_block_capacity = group_allocator_pair.max_block_capacity;

			while (current_copy_group != end_copy_group)
			{
				#ifdef PLF_TYPE_TRAITS_SUPPORT
					if PLF_CONSTEXPR (is_move)
					{
					for (element_pointer_type element_to_copy = current_copy_group->elements; element_to_copy != current_copy_group->end; ++element_to_copy)
						{
							push(std::move(*element_to_copy));
						}
					}
					else
				#endif
				{
					for (element_pointer_type element_to_copy = current_copy_group->elements; element_to_copy != current_copy_group->end; ++element_to_copy)
					{
						push(*element_to_copy);
					}
				}

				current_copy_group = current_copy_group->next_group;
			}

			// Handle special case of last group:
			#ifdef PLF_TYPE_TRAITS_SUPPORT
				if PLF_CONSTEXPR (is_move)
				{
					for (element_pointer_type element_to_copy = source.start_element; element_to_copy != source.top_element + 1; ++element_to_copy)
					{
						push(std::move(*element_to_copy));
					}
				}
				else
			#endif
			{
				for (element_pointer_type element_to_copy = source.start_element; element_to_copy != source.top_element + 1; ++element_to_copy)
				{
					push(*element_to_copy);
				}
			}

			min_block_capacity = source.min_block_capacity;
		}
	}



	PLF_CONSTFUNC void destroy_all_data() PLF_NOEXCEPT
	{
		#ifdef PLF_TYPE_TRAITS_SUPPORT
			if PLF_CONSTEXPR(!std::is_trivially_destructible<element_type>::value) // Avoid iteration for trivially-destructible types eg. POD, structs, classes with empty destructor.
		#endif // If compiler doesn't support traits, iterate regardless - trivial destructors will not be called, hopefully compiler will optimise this loop out for POD types
		{
			if (total_size != 0)
			{
				while (first_group != current_group)
				{
					const element_pointer_type past_end = first_group->end;

					for (element_pointer_type element_pointer = first_group->elements; element_pointer != past_end; ++element_pointer)
					{
						PLF_DESTROY(allocator_type, *this, element_pointer);
					}

					const group_pointer_type next_group = first_group->next_group;
					deallocate_group(first_group);
					first_group = next_group;
				}

				// Special case for current group:
				const element_pointer_type past_end = top_element + 1;

				for (element_pointer_type element_pointer = start_element; element_pointer != past_end; ++element_pointer)
				{
					PLF_DESTROY(allocator_type, *this, element_pointer);
				}

				first_group = first_group->next_group; // To further process reserved groups in the following loop
				deallocate_group(current_group);
			}
		}

		total_size = 0;

		while (first_group != NULL)
		{
			current_group = first_group;
			first_group = first_group->next_group;
			deallocate_group(current_group);
		}
	}



	PLF_CONSTFUNC void blank() PLF_NOEXCEPT
	{
		#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT // allocator_traits and type_traits always available when is_always_equal is available
			if PLF_CONSTEXPR (std::is_standard_layout<stack>::value && std::allocator_traits<allocator_type>::is_always_equal::value && std::is_trivially_destructible<group_pointer_type>::value && std::is_trivially_destructible<element_pointer_type>::value) // if all pointer types are trivial, we can just nuke it from orbit with memset (NULL is always 0 in C++):
			{
				#ifdef PLF_CPP20_SUPPORT
					if PLF_NOT_CONSTEVAL // memset cannot be used during constant evaluation
				#endif
				{
					std::memset(static_cast<void *>(this), 0, offsetof(stack, min_block_capacity));
					return;
				}
			}
		#endif

		current_group = NULL;
		first_group = NULL;
		top_element = NULL;
		start_element = NULL;
		end_element = NULL;
		total_size = 0;
		total_capacity = 0;
	}



public:

	// Copy constructor:
	PLF_CONSTFUNC stack(const stack &source):
		#if (defined(__cplusplus) && __cplusplus >= 201103L) || _MSC_VER >= 1700
			allocator_type(std::allocator_traits<allocator_type>::select_on_container_copy_construction(source)),
		#else
			allocator_type(source),
		#endif
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(source.min_block_capacity),
		group_allocator_pair(source.group_allocator_pair.max_block_capacity, *this)
	{
		copy_from_source(source);
	}



	// Allocator-extended copy constructor:
	PLF_CONSTFUNC stack(const stack &source, const allocator_type &alloc):
		allocator_type(alloc),
		current_group(NULL),
		first_group(NULL),
		top_element(NULL),
		start_element(NULL),
		end_element(NULL),
		total_size(0),
		total_capacity(0),
		min_block_capacity(source.min_block_capacity),
		group_allocator_pair(source.group_allocator_pair.max_block_capacity, alloc)
	{
		copy_from_source(source);
	}



	#ifdef PLF_MOVE_SEMANTICS_SUPPORT
		// move constructor
		PLF_CONSTFUNC stack(stack &&source) PLF_NOEXCEPT:
			allocator_type(std::move(static_cast<allocator_type &>(source))),
			current_group(std::move(source.current_group)),
			first_group(std::move(source.first_group)),
			top_element(std::move(source.top_element)),
			start_element(std::move(source.start_element)),
			end_element(std::move(source.end_element)),
			total_size(source.total_size),
			total_capacity(source.total_capacity),
			min_block_capacity(source.min_block_capacity),
			group_allocator_pair(source.group_allocator_pair.max_block_capacity, source)
		{
			source.blank();
		}


		// allocator-extended move constructor
		#ifdef PLF_CPP20_SUPPORT
			PLF_CONSTFUNC stack(stack &&source, const std::type_identity_t<allocator_type> &alloc):
		#else
			PLF_CONSTFUNC stack(stack &&source, const allocator_type &alloc):
		#endif
			allocator_type(alloc),
			current_group(std::move(source.current_group)),
			first_group(std::move(source.first_group)),
			top_element(std::move(source.top_element)),
			start_element(std::move(source.start_element)),
			end_element(std::move(source.end_element)),
			total_size(source.total_size),
			total_capacity(source.total_capacity),
			min_block_capacity(source.min_block_capacity),
			group_allocator_pair(source.group_allocator_pair.max_block_capacity, alloc)
		{
			#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT
				if PLF_CONSTEXPR (!std::allocator_traits<allocator_type>::is_always_equal::value)
			#endif
			{
				if (alloc != static_cast<allocator_type &>(source))
				{
					blank();
					stack temp(source, alloc);
					move_assign(std::move(temp));
					temp.blank();
					source.destroy_all_data();
				}
			}

			source.blank();
		}
	#endif



	PLF_CONSTFUNC ~stack() PLF_NOEXCEPT
	{
		destroy_all_data();
	}



private:

	PLF_CONSTFUNC void progress_to_next_group() // used by push/emplace
	{
		if (current_group->next_group == NULL) // no reserved groups or groups left over from previous pops, allocate new group
		{
			allocate_new_group((total_size < group_allocator_pair.max_block_capacity) ? total_size : group_allocator_pair.max_block_capacity, current_group);
		}

		current_group = current_group->next_group;
		start_element = top_element = current_group->elements;
		end_element = current_group->end;
	}



 	#if defined(PLF_VARIADICS_SUPPORT) && defined(PLF_MOVE_SEMANTICS_SUPPORT) // emplace and move-insert support
 		#define PLF_PUSH_OBJECT std::forward<arguments>(parameters)...
 		#define PLF_NOTHROW_TEST_TYPE arguments...

 		template<typename... arguments>
 		PLF_CONSTFUNC void push_implementation(arguments &&... parameters)

	#elif defined(PLF_MOVE_SEMANTICS_SUPPORT) // No emplace support, type traits may be available - this is possible under older versions of MSVC, as type_traits were implemented before variadic templates
		#define PLF_PUSH_OBJECT std::forward<el_type>(element)
		#define PLF_NOTHROW_TEST_TYPE el_type

		template<class el_type>
		PLF_CONSTFUNC void push_implementation(el_type &&element)

	#else // Only regular insert support ie. C++03/98 compilers
		#define PLF_PUSH_OBJECT element
		#define PLF_NOTHROW_TEST_TYPE element_type

		PLF_CONSTFUNC void push_implementation(const element_type &element)
	#endif
	{
		if (top_element == NULL) // ie. stack is empty
		{
			top_element = start_element; // reuse the first block, if there is one

			if (top_element == NULL)
			{
				initialize();
			}
		}
		else if (++top_element == end_element)
		{
			progress_to_next_group();
		}


		#ifdef PLF_EXCEPTIONS_SUPPORT
			#ifdef PLF_TYPE_TRAITS_SUPPORT
				if PLF_CONSTEXPR (std::is_nothrow_constructible<element_type, PLF_NOTHROW_TEST_TYPE>::value)
				{
					PLF_CONSTRUCT_ELEMENT(top_element, PLF_PUSH_OBJECT);
				}
				else
			#endif
			{
				try
				{
					PLF_CONSTRUCT_ELEMENT(top_element, PLF_PUSH_OBJECT);
				}
				catch (...)
				{
					if (top_element == start_element)
					{
						if (current_group == first_group) // stack is empty
						{
							top_element = NULL;
							throw;
						}

						current_group = current_group->previous_group;
						start_element = current_group->elements;
						end_element = top_element = current_group->end;
					}

					--top_element;
					throw;
				}
			}
		#else
			PLF_CONSTRUCT_ELEMENT(top_element, PLF_PUSH_OBJECT);
		#endif

		++total_size;
	}

	#undef PLF_PUSH_OBJECT
	#undef PLF_NOTHROW_TEST_TYPE



public:

	PLF_CONSTFUNC void push(const element_type &element)
	{
		push_implementation(element);
	}



	#ifdef PLF_MOVE_SEMANTICS_SUPPORT
		PLF_CONSTFUNC void push(element_type &&element)
		{
			push_implementation(std::move(element));
		}



		#ifdef PLF_VARIADICS_SUPPORT
			template<typename... arguments>
			PLF_CONSTFUNC void emplace(arguments &&... parameters)
			{
				push_implementation(std::forward<arguments>(parameters)...);
			}
		#endif
	#endif



	PLF_CONSTFUNC reference top() const // Exception may occur if stack is empty in release mode
	{
		assert(total_size != 0);
		return *top_element;
	}



	PLF_CONSTFUNC void pop() // Exception may occur if stack is empty
	{
		assert(total_size != 0);

		#ifdef PLF_TYPE_TRAITS_SUPPORT
			if PLF_CONSTEXPR (!std::is_trivially_destructible<element_type>::value)
		#endif
		{
			PLF_DESTROY(allocator_type, *this, top_element);
		}

		--total_size;

		if (top_element != start_element)
		{
			--top_element;
		}
		else if (total_size != 0) // ie. is start element, but not first group in stack
		{
			current_group = current_group->previous_group;
			start_element = current_group->elements;
			end_element = current_group->end;
			top_element = end_element - 1;
		}
		else // stack is now empty - pointing top_element before the start of the block would be undefined behaviour
		{
			top_element = NULL;
		}
	}



	PLF_CONSTFUNC stack & operator = (const stack &source)
	{
		assert(&source != this);

		destroy_all_data();
		blank(); // In case the copy below throws

		#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
			if PLF_CONSTEXPR (std::allocator_traits<allocator_type>::propagate_on_container_copy_assignment::value)
			{
				static_cast<allocator_type &>(*this) = static_cast<const allocator_type &>(source);
				// Reconstruct rebinds:
				static_cast<group_allocator_type &>(group_allocator_pair) = group_allocator_type(*this);
			}
		#endif

		stack temp(source, static_cast<allocator_type &>(*this)); // Copy through this stack's allocator, so that the temporary's blocks can be taken over below

		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			*this = std::move(temp); // Avoid generating 2nd temporary
		#else
			swap(temp);
		#endif

		return *this;
	}



	#ifdef PLF_MOVE_SEMANTICS_SUPPORT
	private:

		PLF_CONSTFUNC void move_assign(stack &&source) PLF_NOEXCEPT_MOVE_ASSIGN(allocator_type)
		{
			#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT
				if PLF_CONSTEXPR_NOT_CONSTEVAL((std::is_trivially_copyable<allocator_type>::value || std::allocator_traits<allocator_type>::is_always_equal::value) &&
					std::is_trivially_copyable<group_pointer_type>::value && std::is_trivially_copyable<element_pointer_type>::value)
				{
					std::memcpy(static_cast<void *>(this), &source, sizeof(stack));
				}
				else
			#endif
			{
				current_group = std::move(source.current_group);
				first_group = std::move(source.first_group);
				top_element = std::move(source.top_element);
				start_element = std::move(source.start_element);
				end_element = std::move(source.end_element);
				total_size = source.total_size;
				total_capacity = source.total_capacity;
				min_block_capacity = source.min_block_capacity;
				group_allocator_pair.max_block_capacity = source.group_allocator_pair.max_block_capacity;

				#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
					if PLF_CONSTEXPR (std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value)
				#endif
				{
					static_cast<allocator_type &>(*this) = static_cast<allocator_type &>(source);
					// Reconstruct rebinds:
					static_cast<group_allocator_type &>(group_allocator_pair) = group_allocator_type(*this);
				}
			}
		}



	public:

		// Move assignment
		PLF_CONSTFUNC stack & operator = (stack &&source) PLF_NOEXCEPT_MOVE_ASSIGN(allocator_type)
		{
			assert (&source != this);

			destroy_all_data();

			#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT
				if PLF_CONSTEXPR (std::allocator_traits<allocator_type>::propagate_on_container_move_assignment::value || std::allocator_traits<allocator_type>::is_always_equal::value)
				{
					move_assign(std::move(source));
				}
				else
			#endif
			if (static_cast<allocator_type &>(*this) == static_cast<allocator_type &>(source))
			{
				move_assign(std::move(source));
			}
			else // Allocator isn't equal so copy elements from source through this stack's allocator and deallocate the source's blocks:
			{
				blank(); // In case the copy below throws
				stack temp(source, static_cast<allocator_type &>(*this));
				move_assign(std::move(temp));
				temp.blank();
				source.destroy_all_data();
			}

			source.blank();
			return *this;
		}
	#endif



	#ifdef PLF_CPP20_SUPPORT
		[[nodiscard]]
	#endif
	PLF_CONSTFUNC bool empty() const PLF_NOEXCEPT
	{
		return total_size == 0;
	}



	PLF_CONSTFUNC size_type size() const PLF_NOEXCEPT
	{
		return total_size;
	}



	PLF_CONSTFUNC size_type max_size() const PLF_NOEXCEPT
	{
		#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
			return std::allocator_traits<allocator_type>::max_size(*this);
		#else
			return allocator_type::max_size();
		#endif
	}



	PLF_CONSTFUNC size_type capacity() const PLF_NOEXCEPT
	{
		return total_capacity;
	}



	PLF_CONSTFUNC size_type memory() const PLF_NOEXCEPT
	{
		size_type memory_use = sizeof(*this) + (sizeof(value_type) * total_capacity);

		for (group_pointer_type current = first_group; current != NULL; current = current->next_group)
		{
			memory_use += sizeof(group);
		}

		return memory_use;
	}



private:

	PLF_CONSTFUNC void consolidate()
	{
		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			stack temp((min_block_capacity > total_size) ? min_block_capacity : ((total_size > group_allocator_pair.max_block_capacity) ? group_allocator_pair.max_block_capacity : total_size), group_allocator_pair.max_block_capacity, static_cast<allocator_type &>(*this)); // Make first allocated block as large as total_size, where possible

			#ifdef PLF_TYPE_TRAITS_SUPPORT
				if PLF_CONSTEXPR (std::is_move_assignable<element_type>::value && std::is_move_constructible<element_type>::value)
				{
					temp.copy_from_source<true>(*this);
				}
				else
			#endif
			{
				temp.copy_from_source(*this);
			}

			temp.min_block_capacity = min_block_capacity; // reset to correct value for future clear() or erasures
			*this = std::move(temp);
		#else
			stack temp(*this, static_cast<allocator_type &>(*this));
			swap(temp);
		#endif
	}



public:


	PLF_CONSTFUNC void reshape(const size_type min, const size_type max)
	{
		check_capacities_conformance(min, max);

		// Need to check all group sizes, because append might append smaller blocks to the end of a larger block:
		for (group_pointer_type current = first_group; current != NULL; current = current->next_group)
		{
			if (static_cast<size_type>(current->end - current->elements) < min || static_cast<size_type>(current->end - current->elements) > max)
			{
				#ifdef PLF_TYPE_TRAITS_SUPPORT // If type is non-copyable/movable, cannot be consolidated, throw exception:
					if PLF_CONSTEXPR (!((std::is_copy_constructible<element_type>::value && std::is_copy_assignable<element_type>::value) || (std::is_move_constructible<element_type>::value && std::is_move_assignable<element_type>::value)))
					{
						#ifdef PLF_EXCEPTIONS_SUPPORT
							throw std::length_error("A memory block capacity is outside of the supplied limits, and the element type is neither copyable nor movable, so the blocks cannot be consolidated");
						#else
							std::terminate();
						#endif
					}
					else
				#endif
				{
					min_block_capacity = min;
					group_allocator_pair.max_block_capacity = max;
					consolidate();
				}

				return;
			}
		}

		min_block_capacity = min;
		group_allocator_pair.max_block_capacity = max;
	}



	PLF_CONSTFUNC void clear() PLF_NOEXCEPT
	{
		destroy_all_data();
		blank();
	}



	friend PLF_CONSTFUNC bool operator == (const stack &lh, const stack &rh) PLF_NOEXCEPT
	{
		if (lh.total_size != rh.total_size) return false;

		for (const_iterator lh_iterator = lh.cbegin(), rh_iterator = rh.cbegin(); lh_iterator != lh.cend(); ++lh_iterator, ++rh_iterator)
		{
			if (*lh_iterator != *rh_iterator) return false;
		}

		return true;
	}



	friend PLF_CONSTFUNC bool operator != (const stack &lh, const stack &rh) PLF_NOEXCEPT
	{
		return !(lh == rh);
	}



	friend PLF_CONSTFUNC bool operator < (const stack &lh, const stack &rh)
	{
		return std::lexicographical_compare(lh.cbegin(), lh.cend(), rh.cbegin(), rh.cend());
	}



	friend PLF_CONSTFUNC bool operator > (const stack &lh, const stack &rh)
	{
		return rh < lh;
	}



	friend PLF_CONSTFUNC bool operator <= (const stack &lh, const stack &rh)
	{
		return !(rh < lh);
	}



	friend PLF_CONSTFUNC bool operator >= (const stack &lh, const stack &rh)
	{
		return !(lh < rh);
	}



	#ifdef PLF_CPP20_SUPPORT
		friend PLF_CONSTFUNC auto operator <=> (const stack &lh, const stack &rh) requires requires (const element_type &element) { { element < element } -> std::convertible_to<bool>; }
		{
			// Use the element's <=> where available, otherwise synthesise an ordering from <
			return std::lexicographical_compare_three_way(lh.cbegin(), lh.cend(), rh.cbegin(), rh.cend(),
				[](const element_type &left, const element_type &right)
				{
					if constexpr (std::three_way_comparable<element_type>)
					{
						return std::compare_three_way()(left, right);
					}
					else
					{
						if (left < right) return std::weak_ordering::less;
						if (right < left) return std::weak_ordering::greater;
						return std::weak_ordering::equivalent;
					}
				});
		}
	#endif



	// Remove trailing stack groups (not removed in general 'pop' usage for performance reasons)
	PLF_CONSTFUNC void trim() PLF_NOEXCEPT
	{
		if (current_group == NULL) return;// ie. stack is empty

		group_pointer_type temp_group = current_group->next_group;
		current_group->next_group = NULL; // Set to NULL regardless of whether it is already NULL (avoids branching). Cuts off rest of groups from this group.

		while (temp_group != NULL)
		{
			const group_pointer_type next_group = temp_group->next_group;
			total_capacity -= static_cast<size_type>(temp_group->end - temp_group->elements);
			deallocate_group(temp_group);
			temp_group = next_group;
		}
	}



	PLF_CONSTFUNC void shrink_to_fit()
	{
		if (first_group == NULL || total_size == total_capacity)
		{
			return;
		}
		else if (total_size == 0) // Edge case
		{
			clear();
			return;
		}

		consolidate();
	}



	PLF_CONSTFUNC void reserve(size_type reserve_amount)
	{
		if (reserve_amount == 0 || reserve_amount <= total_capacity) return;

		reserve_amount -= total_capacity;

		if (reserve_amount < min_block_capacity)
		{
			reserve_amount = min_block_capacity;
		}
		else if (reserve_amount > max_size())
		{
			reserve_amount = max_size();
		}


		size_type number_of_max_capacity_groups = reserve_amount / group_allocator_pair.max_block_capacity,
					remainder = reserve_amount - (number_of_max_capacity_groups * group_allocator_pair.max_block_capacity);

		if (remainder < min_block_capacity) remainder = min_block_capacity;

		if (first_group == NULL) // ie. uninitialized stack
		{
			const size_type original_min_elements = min_block_capacity;

			if (remainder != 0)
			{
				min_block_capacity = remainder;
				remainder = 0;
			}
			else
			{
				min_block_capacity = group_allocator_pair.max_block_capacity;
				--number_of_max_capacity_groups;
			}

			initialize();
			top_element = NULL; // stack is still empty
			min_block_capacity = original_min_elements;
		}


		group_pointer_type temp_group = current_group;

		// navigate to end of all current reserved groups (if they exist):
		while (temp_group->next_group != NULL)
		{
			temp_group = temp_group->next_group;
		}


		if (remainder != 0)
		{
			allocate_new_group(remainder, temp_group);
			temp_group = temp_group->next_group;
		}


		while(number_of_max_capacity_groups != 0)
		{
			allocate_new_group(group_allocator_pair.max_block_capacity, temp_group);
			temp_group = temp_group->next_group;
			--number_of_max_capacity_groups;
		}
	}



	PLF_CONSTFUNC allocator_type get_allocator() const PLF_NOEXCEPT
	{
		return *this;
	}



	PLF_CONSTFUNC void append(stack &source)
	{
		// Process: if the top group of *this is full, link the source's groups after it. Otherwise linking them would leave a gap between the top element and the source's first element, so move or copy the source's elements onto *this instead, in order.

		if (source.total_size == 0)
		{
			return;
		}

		// Throw if incompatible group capacity found, before either stack is changed. Groups after the source's current group are unused and are not transferred, hence they are not checked:
		if (source.min_block_capacity < min_block_capacity || source.group_allocator_pair.max_block_capacity > group_allocator_pair.max_block_capacity)
		{
			for (group_pointer_type current = source.first_group; ; current = current->next_group)
			{
				if (static_cast<size_type>(current->end - current->elements) < min_block_capacity || static_cast<size_type>(current->end - current->elements) > group_allocator_pair.max_block_capacity)
				{
					#ifdef PLF_EXCEPTIONS_SUPPORT
						throw std::length_error("A source memory block capacity is outside of the destination's minimum or maximum memory block capacity limits - please change either the source or the destination's min/max block capacity limits using reshape() before calling append() in this case");
					#else
						std::terminate();
					#endif
				}

				if (current == source.current_group) break;
			}
		}


		if (total_size == 0)
		{
			#ifdef PLF_MOVE_SEMANTICS_SUPPORT
				*this = std::move(source);
			#else
				destroy_all_data();
				blank();
				swap(source);
			#endif

			return;
		}


		#if defined(PLF_TYPE_TRAITS_SUPPORT) && defined(PLF_MOVE_SEMANTICS_SUPPORT)
			if PLF_CONSTEXPR (!std::is_nothrow_move_constructible<element_type>::value)
		#endif
		{
			if (top_element + 1 != end_element) // The top group of *this is not full. Copy the source's elements across, so that both stacks are unchanged if a copy throws. Reserve first, so that copying cannot fail due to allocation:
			{
				reserve(total_size + source.total_size);
				const size_type original_size = total_size;

				#ifdef PLF_EXCEPTIONS_SUPPORT
					try
				#endif
				{
					for (group_pointer_type current = source.first_group; ; current = current->next_group)
					{
						const element_pointer_type past_end = (current == source.current_group) ? source.top_element + 1 : current->end;

						for (element_pointer_type element_pointer = current->elements; element_pointer != past_end; ++element_pointer)
						{
							#ifdef PLF_MOVE_SEMANTICS_SUPPORT
								push_implementation(std::move_if_noexcept(*element_pointer)); // Copies, unless the type cannot be copied
							#else
								push_implementation(*element_pointer);
							#endif
						}

						if (current == source.current_group) break;
					}
				}
				#ifdef PLF_EXCEPTIONS_SUPPORT
					catch (...)
					{
						while (total_size != original_size)
						{
							pop();
						}

						throw;
					}
				#endif

				source.clear();
				return;
			}
		}


		// Trim trailing groups on both, link source and destinations groups and remove references to source groups from source:
		source.trim();
		trim();

		element_pointer_type write = top_element + 1; // The first free location in the top group of *this

		current_group->next_group = source.first_group;
		source.first_group->previous_group = current_group;

		if (write == end_element) // The top group of *this is full, so the source's groups follow on directly
		{
			current_group = source.current_group;
			top_element = source.top_element;
			start_element = source.start_element;
			end_element = source.end_element;
		}
		else // Move each of the source's elements down into the gap left by the free locations in the top group of *this, in order. Element types reaching this point are nothrow-move-constructible
		{
			#if defined(PLF_TYPE_TRAITS_SUPPORT) && defined(PLF_MOVE_SEMANTICS_SUPPORT)
				group_pointer_type write_group = current_group;

				for (group_pointer_type read_group = source.first_group; ; read_group = read_group->next_group)
				{
					const element_pointer_type past_end = (read_group == source.current_group) ? source.top_element + 1 : read_group->end;

					for (element_pointer_type read = read_group->elements; read != past_end; ++read)
					{
						if (write == write_group->end)
						{
							write_group = write_group->next_group;
							write = write_group->elements;
						}

						PLF_CONSTRUCT_ELEMENT(write, std::move(*read));
						++write;
						PLF_DESTROY(allocator_type, source, read);
					}

					if (read_group == source.current_group) break;
				}

				// Any groups after write_group are now unused, and stay allocated, as they would after pops:
				current_group = write_group;
				top_element = write - 1;
				start_element = current_group->elements;
				end_element = current_group->end;
			#endif
		}

		total_size += source.total_size;
		total_capacity += source.total_capacity;

		source.blank();
	}



	PLF_CONSTFUNC void swap(stack &source) PLF_NOEXCEPT_SWAP(allocator_type)
	{
		#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT
			if PLF_CONSTEXPR_NOT_CONSTEVAL(std::allocator_traits<allocator_type>::is_always_equal::value && std::is_trivially_copyable<group_pointer_type>::value && std::is_trivially_copyable<element_pointer_type>::value) // if all pointer types are trivial we can just copy using memcpy - avoids constructors/destructors etc and is faster
			{
				char temp[sizeof(stack)];
				std::memcpy(static_cast<void *>(&temp), static_cast<void *>(this), sizeof(stack));
				std::memcpy(static_cast<void *>(this), static_cast<void *>(&source), sizeof(stack));
				std::memcpy(static_cast<void *>(&source), static_cast<void *>(&temp), sizeof(stack));
			}
			#ifdef PLF_MOVE_SEMANTICS_SUPPORT // If pointer types are not trivial, moving them is probably going to be more efficient than copying them below
				else if PLF_CONSTEXPR (std::is_move_assignable<group_pointer_type>::value && std::is_move_assignable<element_pointer_type>::value && std::is_move_constructible<group_pointer_type>::value && std::is_move_constructible<element_pointer_type>::value && !(std::allocator_traits<allocator_type>::propagate_on_container_swap::value && !std::allocator_traits<allocator_type>::is_always_equal::value)) // move assignment would not swap allocators that propagate on swap only
				{
					stack temp(std::move(source));
					source = std::move(*this);
					*this = std::move(temp);
				}
			#endif
			else
		#endif
		{
			// Otherwise, make the reads/writes as contiguous in memory as-possible (yes, it is faster than using std::swap with the individual variables):
			const group_pointer_type	swap_current_group = current_group, swap_first_group = first_group;
			const element_pointer_type	swap_top_element = top_element, swap_start_element = start_element, swap_end_element = end_element;
			const size_type				swap_total_size = total_size, swap_total_capacity = total_capacity, swap_min_block_capacity = min_block_capacity, swap_max_block_capacity = group_allocator_pair.max_block_capacity;

			current_group = source.current_group;
			first_group = source.first_group;
			top_element = source.top_element;
			start_element = source.start_element;
			end_element = source.end_element;
			total_size = source.total_size;
			total_capacity = source.total_capacity;
			min_block_capacity = source.min_block_capacity;
			group_allocator_pair.max_block_capacity = source.group_allocator_pair.max_block_capacity;

			source.current_group = swap_current_group;
			source.first_group = swap_first_group;
			source.top_element = swap_top_element;
			source.start_element = swap_start_element;
			source.end_element = swap_end_element;
			source.total_size = swap_total_size;
			source.total_capacity = swap_total_capacity;
			source.min_block_capacity = swap_min_block_capacity;
			source.group_allocator_pair.max_block_capacity = swap_max_block_capacity;

			#ifdef PLF_IS_ALWAYS_EQUAL_SUPPORT
				if PLF_CONSTEXPR (std::allocator_traits<allocator_type>::propagate_on_container_swap::value && !std::allocator_traits<allocator_type>::is_always_equal::value)
			#endif
			{
				std::swap(static_cast<allocator_type &>(source), static_cast<allocator_type &>(*this));

				// Reconstruct rebinds for swapped allocators:
				static_cast<group_allocator_type &>(group_allocator_pair) = group_allocator_type(*this);
				static_cast<group_allocator_type &>(source.group_allocator_pair) = group_allocator_type(source);
			} // else: undefined behaviour, as per standard
		}
	}


	// Iterators:

	// Iterator forward declarations:
	template <bool is_const> class			stack_iterator;
	typedef stack_iterator<false>			iterator;
	typedef stack_iterator<true> 			const_iterator;
	friend class stack_iterator<false>;
	friend class stack_iterator<true>;

	template <bool is_const_r> class		stack_reverse_iterator;
	typedef stack_reverse_iterator<false>	reverse_iterator;
	typedef stack_reverse_iterator<true>	const_reverse_iterator;
	friend class stack_reverse_iterator<false>;
	friend class stack_reverse_iterator<true>;



	PLF_CONSTFUNC iterator begin() PLF_NOEXCEPT
	{
		return iterator(first_group, (first_group == NULL) ? NULL : first_group->elements);
	}



	PLF_CONSTFUNC const_iterator begin() const PLF_NOEXCEPT
	{
		return cbegin();
	}



	PLF_CONSTFUNC iterator end() PLF_NOEXCEPT
	{
		if (top_element == NULL) return iterator(first_group, start_element); // ie. empty - same as begin(), as current_group == first_group and start_element == first_group->elements (or both NULL) when the stack is empty
		return ++iterator(current_group, top_element);
	}



	PLF_CONSTFUNC const_iterator end() const PLF_NOEXCEPT
	{
		return cend();
	}



	PLF_CONSTFUNC const_iterator cbegin() const PLF_NOEXCEPT
	{
		return const_iterator(first_group, (first_group == NULL) ? NULL : first_group->elements);
	}



	PLF_CONSTFUNC const_iterator cend() const PLF_NOEXCEPT
	{
		if (top_element == NULL) return const_iterator(first_group, start_element); // ie. empty - same as begin(), as current_group == first_group and start_element == first_group->elements (or both NULL) when the stack is empty
		return ++const_iterator(current_group, top_element);
	}



	PLF_CONSTFUNC reverse_iterator rbegin() PLF_NOEXCEPT
	{
		return reverse_iterator(current_group, top_element);
	}



	PLF_CONSTFUNC const_reverse_iterator rbegin() const PLF_NOEXCEPT
	{
		return crbegin();
	}



	PLF_CONSTFUNC reverse_iterator rend() PLF_NOEXCEPT
	{
		return reverse_iterator(first_group, NULL); // NULL element pointer is the position before the first element
	}



	PLF_CONSTFUNC const_reverse_iterator rend() const PLF_NOEXCEPT
	{
		return crend();
	}



	PLF_CONSTFUNC const_reverse_iterator crbegin() const PLF_NOEXCEPT
	{
		return const_reverse_iterator(current_group, top_element);
	}



	PLF_CONSTFUNC const_reverse_iterator crend() const PLF_NOEXCEPT
	{
		return const_reverse_iterator(first_group, NULL); // NULL element pointer is the position before the first element
	}



	template <bool is_const>
	class stack_iterator
	{
	private:
		typedef typename stack::group_pointer_type 		group_pointer_type;
		typedef typename stack::pointer				 	pointer_type;

		group_pointer_type		group_pointer;
		pointer_type 				element_pointer;

	public:
		struct stack_iterator_tag {};
		typedef std::bidirectional_iterator_tag	iterator_category;
		typedef std::bidirectional_iterator_tag	iterator_concept;
		typedef typename stack::value_type 			value_type;
		typedef typename stack::difference_type		difference_type;
		typedef stack_reverse_iterator<is_const> 	reverse_type;
		typedef typename plf::conditional<is_const, typename stack::const_pointer, typename stack::pointer>::type		pointer;
		typedef typename plf::conditional<is_const, typename stack::const_reference, typename stack::reference>::type	reference;

		friend class stack;
		friend class stack_reverse_iterator<false>;
		friend class stack_reverse_iterator<true>;


		PLF_CONSTFUNC stack_iterator() PLF_NOEXCEPT:
			group_pointer(NULL),
			element_pointer(NULL)
		{}



		PLF_CONSTFUNC stack_iterator (const stack_iterator &source) PLF_NOEXCEPT:
			group_pointer(source.group_pointer),
			element_pointer(source.element_pointer)
		{}



		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_it = is_const, class = typename plf::enable_if<is_const_it>::type >
			PLF_CONSTFUNC stack_iterator(const stack_iterator<false> &source) PLF_NOEXCEPT:
		#else
			PLF_CONSTFUNC stack_iterator(const stack_iterator<!is_const> &source) PLF_NOEXCEPT:
		#endif
			group_pointer(source.group_pointer),
			element_pointer(source.element_pointer)
		{}



		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			PLF_CONSTFUNC stack_iterator(stack_iterator &&source) PLF_NOEXCEPT:
				group_pointer(std::move(source.group_pointer)),
				element_pointer(std::move(source.element_pointer))
			{}



			#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
				template <bool is_const_it = is_const, class = typename plf::enable_if<is_const_it>::type >
				PLF_CONSTFUNC stack_iterator(stack_iterator<false> &&source) PLF_NOEXCEPT:
			#else
				PLF_CONSTFUNC stack_iterator(stack_iterator<!is_const> &&source) PLF_NOEXCEPT:
			#endif
				group_pointer(std::move(source.group_pointer)),
				element_pointer(std::move(source.element_pointer))
			{}
		#endif



		PLF_CONSTFUNC stack_iterator & operator = (const stack_iterator &source) PLF_NOEXCEPT
		{
			group_pointer = source.group_pointer;
			element_pointer = source.element_pointer;
			return *this;
		}



		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_it = is_const, class = typename plf::enable_if<is_const_it>::type >
			PLF_CONSTFUNC stack_iterator & operator = (const stack_iterator<false> &source) PLF_NOEXCEPT
		#else
			PLF_CONSTFUNC stack_iterator & operator = (const stack_iterator<!is_const> &source) PLF_NOEXCEPT
		#endif
		{
			group_pointer = source.group_pointer;
			element_pointer = source.element_pointer;
			return *this;
		}



		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			PLF_CONSTFUNC stack_iterator & operator = (stack_iterator &&source) PLF_NOEXCEPT
			{
				assert(&source != this);
				group_pointer = std::move(source.group_pointer);
				element_pointer = std::move(source.element_pointer);
				return *this;
			}



			#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
				template <bool is_const_it = is_const, class = typename plf::enable_if<is_const_it>::type >
				PLF_CONSTFUNC stack_iterator & operator = (stack_iterator<false> &&source) PLF_NOEXCEPT
			#else
				PLF_CONSTFUNC stack_iterator & operator = (stack_iterator<!is_const> &&source) PLF_NOEXCEPT
			#endif
			{
				group_pointer = std::move(source.group_pointer);
				element_pointer = std::move(source.element_pointer);
				return *this;
			}
		#endif



		PLF_CONSTFUNC bool operator == (const stack_iterator &rh) const PLF_NOEXCEPT
		{
			#ifdef PLF_CPP20_SUPPORT
				if PLF_CONSTEVAL // end() may point one-past-the-end of a different block, and comparing such pointers is not allowed during constant evaluation. Equal positions always have the same group
				{
					return (group_pointer == rh.group_pointer && element_pointer == rh.element_pointer);
				}
			#endif

			return (element_pointer == rh.element_pointer);
		}



		PLF_CONSTFUNC bool operator == (const stack_iterator<!is_const> &rh) const PLF_NOEXCEPT
		{
			#ifdef PLF_CPP20_SUPPORT
				if PLF_CONSTEVAL // end() may point one-past-the-end of a different block, and comparing such pointers is not allowed during constant evaluation. Equal positions always have the same group
				{
					return (group_pointer == rh.group_pointer && element_pointer == rh.element_pointer);
				}
			#endif

			return (element_pointer == rh.element_pointer);
		}



		PLF_CONSTFUNC bool operator != (const stack_iterator &rh) const PLF_NOEXCEPT
		{
			#ifdef PLF_CPP20_SUPPORT
				if PLF_CONSTEVAL // end() may point one-past-the-end of a different block, and comparing such pointers is not allowed during constant evaluation. Equal positions always have the same group
				{
					return !(group_pointer == rh.group_pointer && element_pointer == rh.element_pointer);
				}
			#endif

			return (element_pointer != rh.element_pointer);
		}



		PLF_CONSTFUNC bool operator != (const stack_iterator<!is_const> &rh) const PLF_NOEXCEPT
		{
			#ifdef PLF_CPP20_SUPPORT
				if PLF_CONSTEVAL // end() may point one-past-the-end of a different block, and comparing such pointers is not allowed during constant evaluation. Equal positions always have the same group
				{
					return !(group_pointer == rh.group_pointer && element_pointer == rh.element_pointer);
				}
			#endif

			return (element_pointer != rh.element_pointer);
		}



		PLF_CONSTFUNC reference operator * () const // may cause exception with uninitialized iterator
		{
			return *element_pointer;
		}



		PLF_CONSTFUNC pointer operator -> () const
		{
			return element_pointer;
		}



		PLF_CONSTFUNC stack_iterator & operator ++ ()
		{
			assert(group_pointer != NULL); // covers uninitialised stack_iterator

			if (++element_pointer == group_pointer->end && group_pointer->next_group != NULL)
			{
				group_pointer = group_pointer->next_group;
				element_pointer = group_pointer->elements;
			}

			return *this;
		}



		PLF_CONSTFUNC stack_iterator operator ++(int)
		{
			const stack_iterator copy(*this);
			++*this;
			return copy;
		}



		PLF_CONSTFUNC stack_iterator & operator -- ()
		{
			assert(group_pointer != NULL);

			if (element_pointer == group_pointer->elements)
			{
				if (group_pointer->previous_group == NULL) // First element of the stack - move to the position before it (rend()), without pointing before the start of the block, which would be undefined behaviour
				{
					element_pointer = NULL;
					return *this;
				}

				group_pointer = group_pointer->previous_group;
				element_pointer = group_pointer->end;
			}

			--element_pointer;
			return *this;
		}



		PLF_CONSTFUNC stack_iterator operator -- (int)
		{
			const stack_iterator copy(*this);
			--*this;
			return copy;
		}



	private:
		// Used by cend(), erase() etc:
		PLF_CONSTFUNC stack_iterator(const group_pointer_type group_p, const pointer_type element_p) PLF_NOEXCEPT:
			group_pointer(group_p),
			element_pointer(element_p)
		{}


	}; // stack_iterator




	template <bool is_const_r>
	class stack_reverse_iterator
	{
	private:
		typedef typename stack::group_pointer_type 		group_pointer_type;
		typedef typename stack::pointer				 	pointer_type;

	protected:
		iterator current;

	public:
		struct stack_iterator_tag {};
		typedef std::bidirectional_iterator_tag 	iterator_category;
		typedef std::bidirectional_iterator_tag 	iterator_concept;
		typedef iterator 							iterator_type;
		typedef typename stack::value_type 		value_type;
		typedef typename stack::difference_type	difference_type;
		typedef typename plf::conditional<is_const_r, typename stack::const_pointer, typename stack::pointer>::type		pointer;
		typedef typename plf::conditional<is_const_r, typename stack::const_reference, typename stack::reference>::type	reference;

		friend class stack;


		PLF_CONSTFUNC stack_reverse_iterator () PLF_NOEXCEPT
		{}



		PLF_CONSTFUNC stack_reverse_iterator (const stack_reverse_iterator &source) PLF_NOEXCEPT:
			current(source.current)
		{}


		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
			PLF_CONSTFUNC stack_reverse_iterator (const stack_reverse_iterator<false> &source) PLF_NOEXCEPT:
		#else
			PLF_CONSTFUNC stack_reverse_iterator (const stack_reverse_iterator<!is_const_r> &source) PLF_NOEXCEPT:
		#endif
			current(source.current)
		{}


		PLF_CONSTFUNC stack_reverse_iterator (const stack_iterator<is_const_r> &source) PLF_NOEXCEPT:
			current(source)
		{
			++(*this);
		}


		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
			PLF_CONSTFUNC stack_reverse_iterator (const stack_iterator<false> &source) PLF_NOEXCEPT:
		#else
			PLF_CONSTFUNC stack_reverse_iterator (const stack_iterator<!is_const_r> &source) PLF_NOEXCEPT:
		#endif
			current(source)
		{
			++(*this);
		}


		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			PLF_CONSTFUNC stack_reverse_iterator (stack_reverse_iterator &&source) PLF_NOEXCEPT:
				current(std::move(source.current))
			{}


			#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
				template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
				PLF_CONSTFUNC stack_reverse_iterator (stack_reverse_iterator<false> &&source) PLF_NOEXCEPT:
			#else
				PLF_CONSTFUNC stack_reverse_iterator (stack_iterator<!is_const_r> &&source) PLF_NOEXCEPT:
			#endif
				current(std::move(source.current))
			{}
		#endif


		PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_iterator<is_const_r> &source) PLF_NOEXCEPT
		{
			current = source;
			++current;
			return *this;
		}


		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
			PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_iterator<false> &source) PLF_NOEXCEPT
		#else
			PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_iterator<!is_const_r> &source) PLF_NOEXCEPT
		#endif
		{
			current = source;
			++current;
			return *this;
		}


		PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_reverse_iterator &source) PLF_NOEXCEPT
		{
			current = source.current;
			return *this;
		}


		#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
			template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
			PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_reverse_iterator<false> &source) PLF_NOEXCEPT
		#else
			PLF_CONSTFUNC stack_reverse_iterator& operator = (const stack_reverse_iterator<!is_const_r> &source) PLF_NOEXCEPT
		#endif
		{
			current = source.current;
			return *this;
		}


		#ifdef PLF_MOVE_SEMANTICS_SUPPORT
			PLF_CONSTFUNC stack_reverse_iterator& operator = (stack_reverse_iterator &&source) PLF_NOEXCEPT
			{
				assert(&source != this);
				current = std::move(source.current);
				return *this;
			}


			#ifdef PLF_DEFAULT_TEMPLATE_ARGUMENT_SUPPORT
				template <bool is_const_rit = is_const_r, class = typename plf::enable_if<is_const_rit>::type >
				PLF_CONSTFUNC stack_reverse_iterator& operator = (stack_reverse_iterator<false> &&source) PLF_NOEXCEPT
			#else
				PLF_CONSTFUNC stack_reverse_iterator& operator = (stack_reverse_iterator<!is_const_r> &&source) PLF_NOEXCEPT
			#endif
			{
				assert(&source != this);
				current = std::move(source.current);
				return *this;
			}
		#endif



		PLF_CONSTFUNC bool operator == (const stack_reverse_iterator &rh) const PLF_NOEXCEPT
		{
			return (current == rh.current);
		}



		PLF_CONSTFUNC bool operator == (const stack_reverse_iterator<!is_const_r> &rh) const PLF_NOEXCEPT
		{
			return (current == rh.current);
		}



		PLF_CONSTFUNC bool operator != (const stack_reverse_iterator &rh) const PLF_NOEXCEPT
		{
			return (current != rh.current);
		}



		PLF_CONSTFUNC bool operator != (const stack_reverse_iterator<!is_const_r> &rh) const PLF_NOEXCEPT
		{
			return (current != rh.current);
		}



		PLF_CONSTFUNC reference operator * () const PLF_NOEXCEPT
		{
			return *current.element_pointer;
		}



		PLF_CONSTFUNC pointer operator -> () const PLF_NOEXCEPT
		{
			return current.element_pointer;
		}



		PLF_CONSTFUNC stack_reverse_iterator & operator ++ ()
		{
			--current;
			return *this;
		}



		PLF_CONSTFUNC stack_reverse_iterator operator ++ (int)
		{
			const stack_reverse_iterator copy(*this);
			++*this;
			return copy;
		}



		PLF_CONSTFUNC stack_reverse_iterator & operator -- ()
		{
			if (current.element_pointer == NULL) // ie. rend() - move to the first element
			{
				current.element_pointer = current.group_pointer->elements;
			}
			else
			{
				++current;
			}

			return *this;
		}



		PLF_CONSTFUNC stack_reverse_iterator operator -- (int)
		{
			const stack_reverse_iterator copy(*this);
			--*this;
			return copy;
		}



		PLF_CONSTFUNC stack_iterator<is_const_r> base() const PLF_NOEXCEPT
		{
			if (current.group_pointer == NULL) return stack_iterator<is_const_r>();
			if (current.element_pointer == NULL) return stack_iterator<is_const_r>(current.group_pointer, current.group_pointer->elements); // ie. rend().base() == begin()
			return ++(stack_iterator<is_const_r>(current));
		}



	private:
		// Used by rend(), etc:
		PLF_CONSTFUNC stack_reverse_iterator(const group_pointer_type group_p, const pointer_type element_p) PLF_NOEXCEPT: current(group_p, element_p) {}

	}; // stack_reverse_iterator


}; // stack


#ifdef PLF_CPP20_SUPPORT
	template <class T>
	concept stack_iterator_concept = requires { typename T::stack_iterator_tag; };
#endif

} // plf namespace


namespace std
{

template <class element_type, class allocator_type>
PLF_CONSTFUNC void swap (plf::stack<element_type, allocator_type> &a, plf::stack<element_type, allocator_type> &b) PLF_NOEXCEPT_SWAP(allocator_type)
{
	a.swap(b);
}



#ifdef PLF_CPP20_SUPPORT
	// std::reverse_iterator overload, to allow use of stack with ranges and make_reverse_iterator primarily:
	template <plf::stack_iterator_concept it_type>
	class reverse_iterator<it_type> : public it_type::reverse_type
	{
	public:
		typedef typename it_type::reverse_type rit;
		using rit::rit;
	};
#endif

}


#ifdef PLF_STACK_DEFINES
	#include "plf_tools_undef.h"
#endif

#endif // PLF_STACK_H
