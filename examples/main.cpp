#include <cmath>
#include <iostream>
#include <smart/execution/parallel.hpp>
#include <vector>

int main()
{
    std::vector<double> values(100'000, 1.0);

    smart::for_each(values,
                    [](double& value)
                    {
                        for (int i = 0; i < 200; ++i)
                        {
                            value = std::sqrt(value + 1.0);
                        }
                    });

    std::cout << "Processed " << values.size() << " values.\n";
    return 0;
}
