/* Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>

#include "cnn/problem-config.hpp"

namespace problem
{

std::map<DataType, std::string> DataTypeName;
std::map<std::string, DataType> DataTypeID;
std::vector<unsigned> DataTypeOrder;

std::function<bool(const DataType d)> IsReadWriteDataType;
std::vector<std::function<Point(WorkloadConfig*, const OperationPoint&)>> projectors;

std::ostream& operator << (std::ostream& out, const DataType& d)
{
  out << DataTypeName[d];
  return out;
}

std::map<Dimension, std::string> DimensionName;
std::map<char, Dimension> DimensionID;

std::ostream& operator << (std::ostream& out, const Dimension& dim)
{
  out << DimensionName[dim];
  return out;
}

// ======================================== //
//              Problem Shape               //
// ======================================== //

void BuildProblemShape()
{
  enum class WeightDimension {
    R,
    S,
    C,
    K,
    Num
  };
  enum class InputDimension {
    W,
    H,
    C,
    N,
    Num
  };
  enum class OutputDimension {
    P,
    Q,
    K,
    N,
    Num
  };
  
  DataTypeName = {
    {DataType::Weight, "Weights"},
    {DataType::Input,  "Inputs"},
    {DataType::Output, "Outputs"},
    {DataType::Num,    "Shared/Illegal"}};

  DataTypeID = {
    {"Weights", DataType::Weight},
    {"Inputs", DataType::Input},
    {"Outputs", DataType::Output},
    {"Shared/Illegal", DataType::Num}};

  DataTypeOrder = {
    unsigned(WeightDimension::Num),
    unsigned(InputDimension::Num),
    unsigned(OutputDimension::Num) };

  IsReadWriteDataType = [](const DataType d) -> bool
    {
      // ASSERT(d < DataType::Num);
      return d == DataType::Output;
    };
  
  DimensionName = {{Dimension::R, "R"},
                   {Dimension::S, "S"},
                   {Dimension::P, "P"},
                   {Dimension::Q, "Q"},
                   {Dimension::C, "C"},
                   {Dimension::K, "K"},
                   {Dimension::N, "N"}, };

  DimensionID = {{'R', Dimension::R },
                 {'S', Dimension::S },
                 {'P', Dimension::P },
                 {'Q', Dimension::Q },
                 {'C', Dimension::C },
                 {'K', Dimension::K },
                 {'N', Dimension::N }, };

  projectors =
    {
      [](WorkloadConfig* wc, const OperationPoint& problem_point)
      {
        (void) wc;

        Point weight_point(int(WeightDimension::Num));

        weight_point[int(WeightDimension::R)] = problem_point[int(Dimension::R)];
        weight_point[int(WeightDimension::S)] = problem_point[int(Dimension::S)];
        weight_point[int(WeightDimension::C)] = problem_point[int(Dimension::C)];
        weight_point[int(WeightDimension::K)] = problem_point[int(Dimension::K)];

        return weight_point;
      },
      [](WorkloadConfig* wc, const OperationPoint& problem_point)
      {
        Point input_point(int(InputDimension::Num));
        
        input_point[int(InputDimension::W)] =
          wc->getWstride() * problem_point[int(Dimension::P)] +
          wc->getWdilation() * problem_point[int(Dimension::R)];
        input_point[int(InputDimension::H)] =
          wc->getHstride() * problem_point[int(Dimension::Q)] +
          wc->getHdilation() * problem_point[int(Dimension::S)];
        
        input_point[int(InputDimension::C)] = problem_point[int(Dimension::C)];
        input_point[int(InputDimension::N)] = problem_point[int(Dimension::N)];

        return input_point;
      },
      [](WorkloadConfig* wc, const OperationPoint& problem_point)
      {
        (void) wc;

        Point output_point(int(OutputDimension::Num));
        
        output_point[int(OutputDimension::P)] = problem_point[int(Dimension::P)];
        output_point[int(OutputDimension::Q)] = problem_point[int(Dimension::Q)];
        
        output_point[int(OutputDimension::K)] = problem_point[int(Dimension::K)];
        output_point[int(OutputDimension::N)] = problem_point[int(Dimension::N)];

        return output_point;
      }
    };  
}

// ======================================== //
//             OperationSpace               //
// ======================================== //

OperationSpace::OperationSpace(WorkloadConfig* wc) :
    workload_config_(wc)
{
  for (unsigned space_id = 0; space_id < unsigned(DataType::Num); space_id++)
    data_spaces_.push_back(DataSpace(DataTypeOrder.at(space_id)));
}

OperationSpace::OperationSpace() :
    OperationSpace(nullptr)
{ }

OperationSpace::OperationSpace(WorkloadConfig* wc, const OperationPoint& low, const OperationPoint& high) :
    workload_config_(wc)
{
  for (unsigned space_id = 0; space_id < unsigned(DataType::Num); space_id++)
  {
    auto space_low = projectors.at(space_id)(workload_config_, low);
    auto space_high = projectors.at(space_id)(workload_config_, high);
    // Increment the high points by 1 because the AAHR constructor wants
    // an exclusive max point.
    space_high.IncrementAllDimensions();
    data_spaces_.push_back(DataSpace(DataTypeOrder.at(space_id), space_low, space_high));
  }
}

void OperationSpace::Reset()
{
  for (auto& d : data_spaces_)
    d.Reset();
}

OperationSpace& OperationSpace::operator += (const OperationSpace& s)
{
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    data_spaces_.at(i) += s.data_spaces_.at(i);

  return (*this);
}

OperationSpace& OperationSpace::operator += (const OperationPoint& p)
{
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    data_spaces_.at(i) += projectors.at(i)(workload_config_, p);

  return (*this);
}

OperationSpace OperationSpace::operator - (const OperationSpace& p)
{
  OperationSpace retval(workload_config_);

  for (unsigned i = 0; i < data_spaces_.size(); i++)
    retval.data_spaces_.at(i) = data_spaces_.at(i) - p.data_spaces_.at(i);
  
  return retval;
}

PerDataSpace<std::size_t> OperationSpace::GetSizes() const
{
  PerDataSpace<std::size_t> retval;
  
  for (unsigned i = 0; i < data_spaces_.size(); i++)
    retval.at(i) = data_spaces_.at(i).size();

  return retval;
}

std::size_t OperationSpace::GetSize(const int t) const
{
  return data_spaces_.at(t).size();
}

bool OperationSpace::IsEmpty(const int t) const
{
  return data_spaces_.at(t).empty();
}

bool OperationSpace::CheckEquality(const OperationSpace& rhs, const int t) const
{
  return data_spaces_.at(t) == rhs.data_spaces_.at(t);
}

void OperationSpace::PrintSizes()
{
  for (unsigned i = 0; i < data_spaces_.size()-1; i++)
    std::cout << DataType(i) << " = " << data_spaces_.at(i).size() << ", ";
  std::cout << DataType(data_spaces_.size()-1) << " = " << data_spaces_.back().size() << std::endl;
}

void OperationSpace::Print() const
{
  for (auto& d : data_spaces_)
    d.Print();
}

void OperationSpace::Print(DataType pv) const
{
  auto& d = data_spaces_.at(unsigned(pv));
  d.Print();
}


PerDataSpace<std::size_t> GetMaxWorkingSetSizes(
    problem::PerProblemDimension<int> dimension_sizes)
{
  PerDataSpace<std::size_t> datatype_size;

  datatype_size[DataType::Weight] =
      dimension_sizes[int(Dimension::R)] * dimension_sizes[int(Dimension::S)] *
      dimension_sizes[int(Dimension::C)] * dimension_sizes[int(Dimension::K)];

  datatype_size[DataType::Input] =
      (dimension_sizes[int(Dimension::P)] + dimension_sizes[int(Dimension::R)] - 1) *
      (dimension_sizes[int(Dimension::Q)] + dimension_sizes[int(Dimension::S)] - 1) *
      dimension_sizes[int(Dimension::C)] * dimension_sizes[int(Dimension::N)];

  datatype_size[DataType::Output] =
      dimension_sizes[int(Dimension::P)] * dimension_sizes[int(Dimension::Q)] *
      dimension_sizes[int(Dimension::K)] * dimension_sizes[int(Dimension::N)];
  
  return datatype_size;
}

}  // namespace problem
