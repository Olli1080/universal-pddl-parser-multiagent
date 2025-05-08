
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include <parser/Instance.h>
#include <multiagent/MultiagentDomain.h>
#include <multiagent/ConcurrencyDomain.h>

template<typename T>
void checkEqual(T& prob, const std::string& file)
{
    std::ifstream f(file.c_str());
    if (!f) throw std::runtime_error(std::string("Failed to open file '") + file + "'");
    std::string s, t;

    while (std::getline(f, s)) {
        t += s + "\n";
    }

    std::ostringstream ds;
    ds << prob;
    ASSERT_EQ(t, ds.str());
    std::ofstream of("ins.txt");
    of << ds.str();
}

class MultiagentTests : public testing::Test
{
public:
    
    void multiagentMultilogTest() {
        parser::multiagent::MultiagentDomain dom( "domains/multilog/Multilog_dom.pddl" );
        parser::pddl::Instance ins( dom, "domains/multilog/Multilog_ins.pddl" );

        checkEqual( dom, "expected/multilog/Multilog_dom.pddl" );
        checkEqual( ins, "expected/multilog/Multilog_ins.pddl" );
    }

    void multiagentMazeTest() {
        parser::multiagent::MultiagentDomain dom( "domains/maze/domain/maze_dom_cn.pddl" );
        parser::pddl::Instance ins( dom, "domains/maze/problems/maze5_4_1.pddl" );

        checkEqual( dom, "expected/maze/maze_dom_cn.pddl" );
        checkEqual( ins, "expected/maze/maze5_4_1.pddl" );
    }

    void multiagentWorkshopTest() {
        parser::multiagent::MultiagentDomain dom( "domains/workshop/domain/workshop_dom_cn.pddl" );
        parser::pddl::Instance ins( dom, "domains/workshop/problems/workshop2_2_2_4.pddl" );

        checkEqual( dom, "expected/workshop/workshop_dom_cn.pddl" );
        checkEqual( ins, "expected/workshop/workshop2_2_2_4.pddl" );
    }
};

class ConcurrencyTests : public testing::Test
{
public:
    
    void concurrencyMazeTest() {
        parser::multiagent::ConcurrencyDomain dom( "domains/maze/domain/maze_dom_cal.pddl" );
        parser::pddl::Instance ins( dom, "domains/maze/problems/maze5_4_1.pddl" );

        checkEqual( dom, "expected/maze/maze_dom_cal.pddl" );
        checkEqual( ins, "expected/maze/maze5_4_1.pddl" );
    }

    void concurrencyWorkshopTest() {
        parser::multiagent::ConcurrencyDomain dom( "domains/workshop/domain/workshop_dom_cal.pddl" );
        parser::pddl::Instance ins( dom, "domains/workshop/problems/workshop2_2_2_4.pddl" );

        checkEqual( dom, "expected/workshop/workshop_dom_cal.pddl" );
        checkEqual( ins, "expected/workshop/workshop2_2_2_4.pddl" );
    }

    void concurrencyTablemoverTest() {
        parser::multiagent::ConcurrencyDomain dom( "domains/tablemover/domain/table_domain1.pddl" );
        parser::pddl::Instance ins( dom, "domains/tablemover/problems/table4_2_1.pddl" );

        checkEqual( dom, "expected/tablemover/table_domain1.pddl" );
        checkEqual( ins, "expected/tablemover/table4_2_1.pddl" );
    }
};

TEST_F(MultiagentTests, MultilogTest)
{
    multiagentMultilogTest();
}
TEST_F(MultiagentTests, MazeTest)
{
    multiagentMazeTest();
}
TEST_F(MultiagentTests, WorkshopTest)
{
    multiagentWorkshopTest();
}



TEST_F(ConcurrencyTests, MazeTest)
{
    concurrencyMazeTest();
}

TEST_F(ConcurrencyTests, WorkshopTest)
{
    concurrencyWorkshopTest();
}

TEST_F(ConcurrencyTests, TablemoverTest)
{
    concurrencyTablemoverTest();
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}