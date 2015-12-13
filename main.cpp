#include <iostream>
#include <fstream>
#include <numeric>

#include "gtest/gtest.h"

#include "cell_tree.hpp"
#include "json/src/json.hpp"

using json = nlohmann::json;
using range = memory::Range;

TEST(cell_tree, from_parent_index) {
    // tree with single branch corresponding to the root node
    // this is equivalent to a single compartment model
    //      CASE 1 : single root node in parent_index
    {
        std::vector<int> parent_index = {0};
        cell_tree tree(parent_index);
        EXPECT_EQ(tree.num_branches(), 1);
        EXPECT_EQ(tree.num_children(0), 0);
    }
    //      CASE 2 : empty parent_index
    {
        std::vector<int> parent_index;
        cell_tree tree(parent_index);
        EXPECT_EQ(tree.num_branches(), 1);
        EXPECT_EQ(tree.num_children(0), 0);
    }
    // tree with two branches off the root node
    {
        std::vector<int> parent_index =
            {0, 0, 1, 2, 0, 4};
        cell_tree tree(parent_index);
        EXPECT_EQ(tree.num_branches(), 3);
        // the root has 2 children
        EXPECT_EQ(tree.num_children(0), 2);
        // the children are leaves
        EXPECT_EQ(tree.num_children(1), 0);
        EXPECT_EQ(tree.num_children(2), 0);
    }
    {
        // tree with three branches off the root node
        std::vector<int> parent_index =
            {0, 0, 1, 2, 0, 4, 0, 6, 7, 8};
        cell_tree tree(parent_index);
        EXPECT_EQ(tree.num_branches(), 4);
        // the root has 3 children
        EXPECT_EQ(tree.num_children(0), 3);
        // the children are leaves
        EXPECT_EQ(tree.num_children(1), 0);
        EXPECT_EQ(tree.num_children(2), 0);
        EXPECT_EQ(tree.num_children(3), 0);
    }
    {
        // tree with three branches off the root node, and another 2 branches off of the third branch from the root node
        std::vector<int> parent_index =
            {0, 0, 1, 2, 0, 4, 0, 6, 7, 8, 9, 8, 11, 12};
        cell_tree tree(parent_index);
        EXPECT_EQ(tree.num_branches(), 6);
        // the root has 3 children
        EXPECT_EQ(tree.num_children(0), 3);
        // one of the chilren has 2 children ...
        EXPECT_EQ(tree.num_children(3), 2);
        // the rest are leaves
        EXPECT_EQ(tree.num_children(1), 0);
        EXPECT_EQ(tree.num_children(2), 0);
        EXPECT_EQ(tree.num_children(4), 0);
        EXPECT_EQ(tree.num_children(5), 0);
    }
    {
        //
        //              0
        //             /
        //            1
        //           / \
        //          2   3
        std::vector<int> parent_index = {0,0,1,1};
        cell_tree tree(parent_index);

        EXPECT_EQ(tree.num_branches(), 4);

        EXPECT_EQ(tree.num_children(0), 1);
        EXPECT_EQ(tree.num_children(1), 2);
        EXPECT_EQ(tree.num_children(2), 0);
        EXPECT_EQ(tree.num_children(3), 0);
    }
    {
        //
        //              0
        //             / \
        //            1   2
        //           / \
        //          3   4
        std::vector<int> parent_index = {0,0,0,1,1};
        cell_tree tree(parent_index);

        EXPECT_EQ(tree.num_branches(), 5);

        EXPECT_EQ(tree.num_children(0), 2);
        EXPECT_EQ(tree.num_children(1), 2);
        EXPECT_EQ(tree.num_children(2), 0);
        EXPECT_EQ(tree.num_children(3), 0);
        EXPECT_EQ(tree.num_children(4), 0);
    }
}

TEST(cell_tree, test_balance) {
    {
        // a cell with the following structure
        // should be rebalanced around node 1
        //              0
        //             / \
        //            1   2
        //           / \
        //          3   4
        //             / \
        //            5   6
        std::vector<int> parent_index = {0,0,0,1,1,4,4};
        cell_tree tree(parent_index);

        EXPECT_EQ(tree.num_branches(), 7);

        EXPECT_EQ(tree.num_children(0), 2);
        EXPECT_EQ(tree.num_children(1), 2);
        EXPECT_EQ(tree.num_children(2), 0);
        EXPECT_EQ(tree.num_children(3), 0);
        EXPECT_EQ(tree.num_children(4), 2);
        EXPECT_EQ(tree.num_children(5), 0);
        EXPECT_EQ(tree.num_children(6), 0);
    }
}

/*
void test_json() {
    json  cell_data;
    std::ifstream("cells_small.json") >> cell_data;

    for(auto c : range(0,20)) {
    //for(auto c : range(0,cell_data.size())) {
        std::vector<int> parent_index = cell_data[c]["parent_index"];
        cell_tree tree(parent_index);
        std::cout << "cell " << c << " ";
        tree.balance();
        tree.to_graphviz("cell_" + std::to_string(c) + ".dot");
        std::cout << memory::util::yellow("---------") << std::endl;
    }
}
*/

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
