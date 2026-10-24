#include "plf_tools.h"

#include <cstdio> // log redirection
#include <cstdlib> // abort

#ifdef PLF_MOVE_SEMANTICS_SUPPORT
	#include <utility> // std::move
#endif

#include <algorithm> // std::sort
#include <limits> // std::numeric_limits
#include <map>
#include <vector>

#ifdef PLF_CPP20_SUPPORT
	#include <compare> // std::strong_ordering, std::weak_ordering, std::partial_ordering, std::three_way_comparable
	#include <type_traits> // std::is_same_v
#endif

#include "plf_stack.h"

#ifdef PLF_ALLOCATOR_TRAITS_SUPPORT
	#include <map> // stateful allocator testing
#endif




// Element types for the comparison operator tests:
struct less_only_type // Has < but not <=>
{
	int value;

	less_only_type(const int v) : value(v) {}
	bool operator < (const less_only_type &rh) const { return value < rh.value; }
	bool operator != (const less_only_type &rh) const { return value != rh.value; }
};


struct unordered_type // Has != (for ==) but no ordering
{
	int value;

	unordered_type(const int v) : value(v) {}
	bool operator != (const unordered_type &rh) const { return value != rh.value; }
};


#ifdef PLF_CPP20_SUPPORT
	struct non_bool_result // Result of non_bool_less_type's <, cannot be used as a bool
	{
		explicit operator bool() const = delete;
	};


	struct non_bool_less_type // Has < but its result is not convertible to bool
	{
		int value;

		non_bool_less_type(const int v) : value(v) {}
		non_bool_result operator < (const non_bool_less_type &) const { return non_bool_result(); }
		bool operator != (const non_bool_less_type &rh) const { return value != rh.value; }
	};
#endif


template <class container_type, class value_type>
void push_values(container_type &container, const value_type *values, const unsigned int count)
{
	for (unsigned int index = 0; index != count; ++index)
	{
		container.push(values[index]);
	}
}




void title1(const char *title_text)
{
	printf("\n\n\n*** %s ***\n", title_text);
	printf("===========================================\n\n\n");
}

void title2(const char *title_text)
{
	printf("\n\n--- %s ---\n\n", title_text);
}


void failpass(const char *test_type, bool condition)
{
	printf("%s: ", test_type);

	if (condition)
	{
		printf("Pass\n");
	}
	else
	{
		printf("Fail\n");
		getchar();
		abort();
	}
}




#ifdef PLF_VARIADICS_SUPPORT
	struct perfect_forwarding_test
	{
		const bool success;

		perfect_forwarding_test(int && /*perfect1*/, int& perfect2)
			: success(true)
		{
			perfect2 = 1;
		}

		template <typename T, typename U>
		perfect_forwarding_test(T&& /*imperfect1*/, U&& /*imperfect2*/)
			: success(false)
		{}
	};
#endif


#if defined(PLF_TYPE_TRAITS_SUPPORT) && (__cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L))
	struct non_copyable_non_movable
	{
		int value;

		non_copyable_non_movable(const int v) : value(v) {}
		non_copyable_non_movable(const non_copyable_non_movable &) = delete;
		non_copyable_non_movable & operator = (const non_copyable_non_movable &) = delete;
	};
#endif



#if defined(PLF_ALLOCATOR_TRAITS_SUPPORT) && defined(PLF_MOVE_SEMANTICS_SUPPORT)
	// Stateful allocator: every block must be allocated through an allocator that was not default-constructed, and freed through one equal to the allocator that allocated it
	std::map<void *, int> allocation_owners;
	int allocator_mismatches = 0;

	template <class value_type_, bool propagate = true>
	struct id_allocator
	{
		typedef value_type_ value_type;
		typedef std::false_type is_always_equal;
		typedef typename plf::conditional<propagate, std::true_type, std::false_type>::type propagate_on_container_copy_assignment;
		typedef propagate_on_container_copy_assignment propagate_on_container_move_assignment;
		typedef std::true_type propagate_on_container_swap;

		template <class other_type> struct rebind { typedef id_allocator<other_type, propagate> other; };

		int id;

		id_allocator() PLF_NOEXCEPT: id(0) {} // Nothing may be allocated through a default-constructed allocator
		explicit id_allocator(const int new_id) PLF_NOEXCEPT: id(new_id) {}
		id_allocator(const id_allocator &source) PLF_NOEXCEPT: id(source.id) {}
		template <class other_type> id_allocator(const id_allocator<other_type, propagate> &source) PLF_NOEXCEPT: id(source.id) {}
		id_allocator & operator = (const id_allocator &source) PLF_NOEXCEPT { id = source.id; return *this; }

		value_type * allocate(const std::size_t size)
		{
			value_type * const allocation = static_cast<value_type *>(::operator new(size * sizeof(value_type)));
			if (id == 0) ++allocator_mismatches;
			allocation_owners[allocation] = id;
			return allocation;
		}

		void deallocate(value_type * const allocation, const std::size_t) PLF_NOEXCEPT
		{
			const std::map<void *, int>::iterator owner = allocation_owners.find(allocation);
			if (owner == allocation_owners.end() || owner->second != id) ++allocator_mismatches;
			if (owner != allocation_owners.end()) allocation_owners.erase(owner);
			::operator delete(allocation);
		}

		template <class other_type> bool operator == (const id_allocator<other_type, propagate> &source) const PLF_NOEXCEPT { return id == source.id; }
		template <class other_type> bool operator != (const id_allocator<other_type, propagate> &source) const PLF_NOEXCEPT { return id != source.id; }
	};
#endif



int main()
{
	freopen("error.log","w", stderr);

	using namespace std;
	using namespace plf;


	unsigned int looper = 0;


	while (++looper != 50)
	{
		{
			title1("Test basics");

			stack<unsigned int> i_stack(50);

			for (unsigned int temp = 0; temp != 250000; ++temp)
			{
				i_stack.push(10);
			}

			failpass("Multipush test", i_stack.size() == 250000);

			stack<unsigned int> i_stack2;
			i_stack2 = i_stack;

			stack<unsigned int> i_stack3(i_stack);

			failpass("Copy constructor test", i_stack3.size() == 250000);

			stack<unsigned int> i_stack6(i_stack, i_stack3.get_allocator());

			failpass("Allocator-extended copy constructor test", i_stack6.size() == 250000);

			i_stack3.reserve(400000);

			failpass("Reserve test", i_stack3.size() == 250000);


			stack<unsigned int> i_stack7(50, 50);

			for (unsigned int temp = 0; temp != 449; ++temp)
			{
				i_stack7.push(10);
			}

			failpass("Max limit test", i_stack7.capacity() == 450);

			i_stack7.reshape(100, 100);

			failpass("Reshape test", i_stack7.capacity() == 500);


			#ifdef PLF_MOVE_SEMANTICS_SUPPORT
				stack<unsigned int> i_stack4;
				i_stack4 = std::move(i_stack3);
				failpass("Move equality operator test", i_stack2 == i_stack4);
				stack<unsigned int> i_stack5(std::move(i_stack4), i_stack3.get_allocator());

				failpass("Allocator-extended move-construct test", i_stack5.size() == 250000);

				i_stack3 = std::move(i_stack5);

			#else
				failpass("Equality operator test", i_stack2 == i_stack3);
			#endif

			failpass("Copy test", i_stack2.size() == 250000);
			failpass("Equality operator test 2", i_stack == i_stack2);

			i_stack2.push(5);

 			failpass("Inequality operator test", i_stack != i_stack2);

			i_stack2.swap(i_stack3);

			failpass("Swap test", i_stack2.size() == i_stack3.size() - 1);

			swap(i_stack2, i_stack3);

			failpass("Swap test 2", i_stack3.size() == i_stack2.size() - 1);

			failpass("max_size() test", i_stack2.max_size() > i_stack2.size());


			unsigned int total = 0;

			const unsigned int temp_capacity = static_cast<unsigned int>(i_stack.capacity());

			for (unsigned int temp = 0; temp != 200000; ++temp)
			{
				total += i_stack.top();
				i_stack.pop();
			}

			failpass("Multipop test", i_stack.size() == 50000);
			failpass("top() test", total == 2000000);

			i_stack.shrink_to_fit();

			failpass("shrink_to_fit() test", temp_capacity != i_stack.capacity());


			do
			{
				if ((rand() & 3) == 0)
				{
					i_stack.push(10);
				}
				else
				{
					i_stack.pop();
				}
			} while (!i_stack.empty());;

			failpass("Randomly pop/push till empty test", i_stack.empty());

			#ifdef PLF_VARIADICS_SUPPORT
				i_stack.emplace(20);
				failpass("Emplace test", i_stack.size() == 1);
			#endif
		}

		{
			title2("Stack Special Case Tests");

			stack<int> i_stack(50, 100);

			for (int temp = 0; temp != 256; ++temp)
			{
				i_stack.push(10);
			}

			stack<int> i_stack_copy(i_stack);

			int temp2 = 0;

			for (int temp = 0; temp != 256; ++temp)
			{
				temp2 += i_stack_copy.top();
				i_stack_copy.pop();
			}

			failpass("Stack copy special case test", temp2 == 2560);
		}


		{
 			title2("stack expansion test");

 			stack<int> i_stack;
			int push_total = 0, pop_total = 0;

			for (int counter = 0; counter != 500; ++counter)
			{
				i_stack.push(counter);
				push_total += counter;
			}


 			do
 			{
				pop_total += i_stack.top();
				i_stack.pop();
 			} while (!i_stack.empty());


			failpass("stack expansion test 1", pop_total == push_total);


			push_total = 0;
			pop_total = 0;


			for (int counter = 0; counter != 50; ++counter)
			{
				i_stack.push(counter);
				push_total += counter;
			}


 			do
 			{
 				if ((rand() & 3) == 0)
 				{
 					pop_total += i_stack.top();
					i_stack.pop();
 				}
 				else
 				{
 					i_stack.push(10);
					push_total += 10;
 				}
 			} while (!i_stack.empty() && i_stack.capacity() < 5000);



 			do
 			{
 				if ((rand() & 3) == 0)
 				{
 					i_stack.push(10);
					push_total += 10;
 				}
 				else
 				{
 					pop_total += i_stack.top();
					i_stack.pop();
 				}
 			} while (!i_stack.empty());


 			failpass("random stack expansion test", pop_total == push_total);
		}



		#ifdef PLF_VARIADICS_SUPPORT
		{
			title2("Perfect Forwarding tests");

			stack<perfect_forwarding_test> pf_stack;

			int lvalue = 0;
			int &lvalueref = lvalue;

			pf_stack.emplace(7, lvalueref);

			failpass("Perfect forwarding test", pf_stack.top().success);
			failpass("Perfect forwarding test 2", lvalueref == 1);
		}
		#endif


		#if defined(PLF_TYPE_TRAITS_SUPPORT) && defined(PLF_VARIADICS_SUPPORT) && defined(PLF_EXCEPTIONS_SUPPORT) && (__cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)) // before C++17, reshape() cannot compile for this type because consolidate() is instantiated without if constexpr
		{
			title2("Reshape non-copyable non-movable type test");

			stack<non_copyable_non_movable> ncm_stack;
			ncm_stack.emplace(1);

			const stack<non_copyable_non_movable>::size_type capacity_before = ncm_stack.capacity();
			bool threw_length_error = false;

			try
			{
				ncm_stack.reshape(2, 2); // the existing block is outside these limits and cannot be consolidated
			}
			catch (const std::length_error &)
			{
				threw_length_error = true;
			}

			failpass("Reshape non-copyable non-movable type throws length_error", threw_length_error);
			failpass("Reshape non-copyable non-movable type leaves stack unchanged", ncm_stack.size() == 1 && ncm_stack.top().value == 1 && ncm_stack.capacity() == capacity_before);

			// The rejected limits must not be kept: the next block should not be capped at 2 elements
			while (ncm_stack.size() != capacity_before)
			{
				ncm_stack.emplace(1);
			}

			ncm_stack.emplace(1);
			failpass("Reshape non-copyable non-movable type keeps the old block capacity limits", ncm_stack.capacity() - capacity_before > 2);
		}
		#endif


		{
			title2("append tests");

			{
				stack<int> stack1, stack2;

				for(int number = 0; number != 20; ++number)
				{
					stack1.push(number);
					stack2.push(number + 20);
				}

				stack1.append(stack2);

				int check_number = 0;

				while(!stack1.empty())
				{
					check_number += stack1.top();
					stack1.pop();
				}

				failpass("Small append test 1", check_number == 780);
			}


			{
				stack<unsigned int> stack1, stack2;

				for(unsigned int number = 0; number != 100000; ++number)
				{
					stack1.push(number);
					stack2.push(number + 100000);
				}

				stack1.append(stack2);

				unsigned int check_number = 0;

				while(!stack1.empty())
				{
					check_number += stack1.top();
					stack1.pop();
				}

				failpass("Large append test 1", check_number == 2820030816u);
			}



			{
				stack<int> stack1, stack2;

				for(int number = 150; number != 250; ++number)
				{
					stack1.push(number);
				}


				for(int number = 0; number != 150; ++number)
				{
					stack2.push(number);
				}


				stack1.append(stack2);

				int check_number = 0;

				while(!stack1.empty())
				{
					check_number += stack1.top();
					stack1.pop();
				}


				failpass("Unequal size append test 1", check_number == 31125);
			}


			{ // The source's blocks are linked after the destination's full top block, so their capacity has to be added
				stack<int> stack1(3, 3), stack2(3, 3);

				for (int number = 0; number != 3; ++number)
				{
					stack1.push(number); // one full block
				}

				for (int number = 0; number != 6; ++number)
				{
					stack2.push(number); // two full blocks
				}

				stack1.append(stack2);

				failpass("Append capacity test", stack1.size() == 9 && stack1.capacity() == 9);

				for (int number = 0; number != 6; ++number)
				{
					stack1.pop();
				}

				stack1.trim(); // deallocates the two blocks from stack2

				failpass("Append capacity after trim test", stack1.size() == 3 && stack1.capacity() == 3);
			}


			#ifdef PLF_EXCEPTIONS_SUPPORT
			{ // The block capacity limits are checked before either stack is changed
				stack<int> stack1(3, 3), stack2(10, 10);
				stack1.push(1);

				for (int number = 10; number != 15; ++number)
				{
					stack2.push(number); // one block of 10, larger than stack1's maximum block capacity
				}

				bool threw_length_error = false;

				try
				{
					stack1.append(stack2);
				}
				catch (const std::length_error &)
				{
					threw_length_error = true;
				}

				failpass("Append with incompatible block capacities throws length_error test", threw_length_error);
				failpass("Append with incompatible block capacities leaves both stacks unchanged test", stack1.size() == 1 && stack1.top() == 1 && stack2.size() == 5 && stack2.top() == 14);

				stack1.pop();

				int check_number = 14;
				bool stack2_unchanged = true;

				while (!stack2.empty())
				{
					stack2_unchanged = stack2_unchanged && stack2.top() == check_number--;
					stack2.pop();
				}

				failpass("Append with incompatible block capacities pop test", stack1.empty() && stack2_unchanged && check_number == 9);
			}


			{ // The same check applies when the destination is empty, rather than moving the source in
				stack<int> stack1(3, 3), stack2(10, 10);

				for (int number = 10; number != 15; ++number)
				{
					stack2.push(number);
				}

				bool threw_length_error = false;

				try
				{
					stack1.append(stack2);
				}
				catch (const std::length_error &)
				{
					threw_length_error = true;
				}

				failpass("Append to an empty stack with incompatible block capacities throws length_error test", threw_length_error && stack1.empty() && stack2.size() == 5 && stack2.top() == 14);
			}
			#endif
		}



		{
			title1("Iterator tests");

			stack<int> istack;
			int total = 0;

			for (int temp = 0; temp != 1000; ++temp)
			{
				istack.push(1);
				++total;
			}

			int number_of_elements = static_cast<int>(istack.size());

			for (stack<int>::iterator current = istack.begin(); current != istack.end(); ++current)
			{
				total -= *current;
				--number_of_elements;
			}

			failpass("Iterator test 1", total == 0 && number_of_elements == 0);

			istack.pop();
			istack.pop();
			istack.pop();
			istack.pop();
			istack.pop();

			number_of_elements = static_cast<int>(istack.size());

			for (stack<int>::iterator current = istack.begin(); current != istack.end(); ++current)
			{
				--number_of_elements;
			}

			failpass("Iterator test 2", number_of_elements == 0);

			istack.push(1);
			istack.push(1);

			number_of_elements = static_cast<int>(istack.size());

			for (stack<int>::reverse_iterator current = istack.rbegin(); current != istack.rend(); ++current)
			{
				--number_of_elements;
			}

			failpass("Reverse Iterator test 1", number_of_elements == 0);

			{ // The top element is the last in its block, and the block after it was emptied by pop() and kept
				stack<int> kept_stack(3, 3);

				for (int counter = 0; counter != 4; ++counter)
				{
					kept_stack.push(counter);
				}

				kept_stack.pop();

				number_of_elements = 0;

				for (stack<int>::iterator current = kept_stack.begin(); current != kept_stack.end(); ++current)
				{
					if (++number_of_elements == 4) break; // There are only three elements
				}

				failpass("Iterator test with a kept block after the top block", number_of_elements == 3 && *--kept_stack.end() == 2);

				stack<int> compare_stack(3, 3);

				for (int counter = 0; counter != 3; ++counter)
				{
					compare_stack.push(counter);
				}

				failpass("Equality operator test with a kept block after the top block", kept_stack == compare_stack);
			}

		}


		{
			title1("Comparison operator tests");

			// Block capacity 3, so containers with more than 3 elements span several blocks
			stack<int> empty1(3, 3), empty2(3, 3), one(3, 3), one_two(3, 3), one_three(3, 3), one_two_zero(3, 3), two_one(3, 3), long1(3, 3), long2(3, 3), long_differs(3, 3);

			const int one_values[] = {1}, one_two_values[] = {1, 2}, one_three_values[] = {1, 3}, one_two_zero_values[] = {1, 2, 0}, two_one_values[] = {2, 1};
			const int long_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, long_differs_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10};

			push_values(one, one_values, 1);
			push_values(one_two, one_two_values, 2);
			push_values(one_three, one_three_values, 2);
			push_values(one_two_zero, one_two_zero_values, 3);
			push_values(two_one, two_one_values, 2);
			push_values(long1, long_values, 10);
			push_values(long2, long_values, 10);
			push_values(long_differs, long_differs_values, 10);

			failpass("Comparison of two empty stacks", !(empty1 < empty2) && !(empty1 > empty2) && empty1 <= empty2 && empty1 >= empty2);
			failpass("Empty stack is less than non-empty stack", empty1 < one && one > empty1 && empty1 <= one && !(empty1 >= one));
			failpass("Comparison of stacks differing in the last element", one_two < one_three && one_three > one_two && !(one_three < one_two));
			failpass("Prefix stack is less than longer stack", one_two < one_two_zero && !(one_two_zero < one_two) && one_two <= one_two_zero);
			failpass("Elements compared bottom to top", one_two < two_one && !(two_one < one_two));
			failpass("Comparison of equal multi-block stacks", !(long1 < long2) && !(long1 > long2) && long1 <= long2 && long1 >= long2);
			failpass("Comparison of multi-block stacks differing in the last block", long1 < long_differs && long_differs > long1 && !(long_differs <= long1));

			{ // The first has a kept empty block after its top block, the second has a different block capacity
				stack<int> popped_stack(3, 3), other_stack(4, 100);

				for (int counter = 0; counter != 10; ++counter)
				{
					popped_stack.push(counter);
				}

				for (int counter = 0; counter != 4; ++counter)
				{
					popped_stack.pop();
				}

				for (int counter = 0; counter != 6; ++counter)
				{
					other_stack.push(counter);
				}

				failpass("Comparison of equal stacks with different block layouts", !(popped_stack < other_stack) && !(other_stack < popped_stack) && popped_stack <= other_stack && popped_stack >= other_stack);

				#ifdef PLF_CPP20_SUPPORT
					failpass("Three-way comparison of equal stacks with different block layouts", (popped_stack <=> other_stack) == 0);
				#endif

				other_stack.push(10);

				failpass("Comparison of stacks with different block layouts differing in length", popped_stack < other_stack && !(other_stack <= popped_stack));
			}

			std::map<stack<int>, int> stack_map;
			stack_map[one_two] = 1;
			stack_map[empty1] = 2;
			stack_map[one_three] = 3;
			stack_map[one_two] = 4; // Existing key

			failpass("stack as std::map key test", stack_map.size() == 3 && stack_map.begin()->second == 2 && stack_map[one_two] == 4);

			std::vector<stack<int> > stack_vector;
			stack_vector.push_back(one_three);
			stack_vector.push_back(empty1);
			stack_vector.push_back(one_two);
			std::sort(stack_vector.begin(), stack_vector.end());

			failpass("std::sort on stacks test", stack_vector[0] == empty1 && stack_vector[1] == one_two && stack_vector[2] == one_three);

			stack<less_only_type> less_only1, less_only2;
			less_only1.push(less_only_type(1));
			less_only2.push(less_only_type(2));

			failpass("Comparison of stacks of an element type with only <", less_only1 < less_only2 && !(less_only2 <= less_only1));

			stack<unordered_type> unordered1, unordered2; // Comparison operators must not be instantiated for this type
			unordered1.push(unordered_type(1));
			unordered2.push(unordered_type(1));

			failpass("Equality of stacks of an element type with no ordering", unordered1 == unordered2);

			#ifdef PLF_CPP20_SUPPORT
				static_assert(std::is_same_v<decltype(one_two <=> one_three), std::strong_ordering>);
				failpass("Three-way comparison test", (one_two <=> one_three) < 0 && (two_one <=> one_two) > 0 && (long1 <=> long2) == 0 && (empty1 <=> one) < 0);

				static_assert(std::is_same_v<decltype(less_only1 <=> less_only2), std::weak_ordering>);
				failpass("Three-way comparison of an element type with only <", (less_only1 <=> less_only2) == std::weak_ordering::less);

				stack<double> nan_stack, double_stack;
				nan_stack.push(std::numeric_limits<double>::quiet_NaN());
				double_stack.push(1.0);

				static_assert(std::is_same_v<decltype(nan_stack <=> double_stack), std::partial_ordering>);
				failpass("Three-way comparison of an element type with partial ordering", (nan_stack <=> double_stack) == std::partial_ordering::unordered);

				static_assert(std::three_way_comparable<stack<int>, std::strong_ordering>);
				static_assert(!std::three_way_comparable<stack<unordered_type> >);
				static_assert(!std::three_way_comparable<stack<non_bool_less_type> >);
			#endif
		}
	}


	#if defined(PLF_ALLOCATOR_TRAITS_SUPPORT) && defined(PLF_MOVE_SEMANTICS_SUPPORT)
	{
		title2("Stateful allocator tests");

		{
			typedef stack<int, id_allocator<int> > id_container;

			id_container container1(id_allocator<int>(1));

			for (int counter = 0; counter != 1000; ++counter)
			{
				container1.push(counter);
			}

			for (int counter = 0; counter != 500; ++counter)
			{
				container1.pop();
			}

			container1.reshape(10, 50);
			container1.shrink_to_fit();
			container1.trim();

			id_container container2(container1);
			id_container container3(container1, id_allocator<int>(3));
			id_container container4(std::move(container2));
			id_container container5(std::move(container3), id_allocator<int>(5)); // unequal allocators, so elements are moved individually

			id_container container6(id_allocator<int>(6));
			container6.push(1);
			container6 = container1; // propagates allocator 1
			id_container container7(id_allocator<int>(7));
			container7.push(1);
			container7 = std::move(container4); // propagates allocator 1
			container6.swap(container5); // swaps allocators 1 and 5

			for (int counter = 0; counter != 100; ++counter)
			{
				container5.push(counter);
				container6.push(counter);
				container7.push(counter);
			}

			failpass("Stateful allocator get_allocator test", container1.get_allocator().id == 1 && container5.get_allocator().id == 1 && container6.get_allocator().id == 5 && container7.get_allocator().id == 1);

			typedef stack<int, id_allocator<int, false> > id_container_no_propagation;

			id_container_no_propagation container8(id_allocator<int, false>(8));

			for (int counter = 0; counter != 1000; ++counter)
			{
				container8.push(counter);
			}

			container8.reshape(10, 50);
			container8.shrink_to_fit();

			failpass("Stateful allocator reshape test", container8.get_allocator().id == 8 && container8.size() == 1000);

			id_container_no_propagation container9(id_allocator<int, false>(9));
			container9.push(1);
			container9 = container8; // keeps allocator 9
			id_container_no_propagation container10(id_allocator<int, false>(10));
			container10.push(1);
			container10 = std::move(container8); // keeps allocator 10, so the elements are copied
			container9.swap(container10); // swaps allocators 9 and 10

			failpass("Stateful allocator no-propagation test", container9.get_allocator().id == 10 && container10.get_allocator().id == 9 && container9.size() == 1000 && container10.size() == 1000 && container8.empty());
		}

		failpass("Stateful allocator test", allocator_mismatches == 0 && allocation_owners.empty());
	}
	#endif

	title1("Test Suite PASS - Press ENTER to Exit");
	getchar();

	return 0;
}
