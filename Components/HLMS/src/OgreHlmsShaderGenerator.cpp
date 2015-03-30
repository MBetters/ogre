/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2015 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreHlmsShaderGenerator.h"

namespace Ogre
{
	//-----------------------------------------------------------------------------------
	void ShaderGenerator::findBlockEnd(SubStringRef &outSubString, bool &syntaxError)
	{
		const char *blockNames[] =
		{
			"foreach",
			"property",
			"piece"
		};

		Ogre::String::const_iterator it = outSubString.begin();
		Ogre::String::const_iterator en = outSubString.end();

		int nesting = 0;

		while (it != en && nesting >= 0)
		{
			if (*it == '@')
			{
				SubStringRef subString(&outSubString.getOriginalBuffer(), it + 1);

				size_t idx = subString.find("end");
				if (idx == 0)
				{
					--nesting;
					it += sizeof("end") - 1;
				}
				else
				{
					for (size_t i = 0; i < sizeof(blockNames) / sizeof(char*); ++i)
					{
						size_t idxBlock = subString.find(blockNames[i]);
						if (idxBlock == 0)
						{
							it = subString.begin() + strlen(blockNames[i]);
							++nesting;
							break;
						}
					}
				}
			}

			++it;
		}

		assert(nesting >= -1);

		if (it != en && nesting < 0)
			outSubString.setEnd(it - outSubString.getOriginalBuffer().begin() - sizeof("end"));
		else
		{
			syntaxError = false;
			printf("Syntax Error at line %lu: start block (e.g. @foreach; @property) "
				"without matching @end\n", calculateLineCount(outSubString));
		}
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::evaluateExpression(SubStringRef &outSubString, bool &outSyntaxError, PropertyMap &properties)
	{
		size_t expEnd = evaluateExpressionEnd(outSubString);

		if (expEnd == Ogre::String::npos)
		{
			outSyntaxError = true;
			return false;
		}

		SubStringRef subString(&outSubString.getOriginalBuffer(), outSubString.getStart(),
			outSubString.getStart() + expEnd);

		outSubString = SubStringRef(&outSubString.getOriginalBuffer(),
			outSubString.getStart() + expEnd + 1);

		bool textStarted = false;
		bool syntaxError = false;
		bool nextExpressionNegates = false;

		std::vector<Expression*> expressionParents;
		ExpressionVec outExpressions;
		outExpressions.clear();
		outExpressions.resize(1);

		Expression *currentExpression = &outExpressions.back();

		Ogre::String::const_iterator it = subString.begin();
		Ogre::String::const_iterator en = subString.end();

		while (it != en && !syntaxError)
		{
			char c = *it;

			if (c == '(')
			{
				currentExpression->children.push_back(Expression());
				expressionParents.push_back(currentExpression);

				currentExpression->children.back().negated = nextExpressionNegates;

				textStarted = false;
				nextExpressionNegates = false;

				currentExpression = &currentExpression->children.back();
			}
			else if (c == ')')
			{
				if (expressionParents.empty())
					syntaxError = true;
				else
				{
					currentExpression = expressionParents.back();
					expressionParents.pop_back();
				}

				textStarted = false;
			}
			else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				textStarted = false;
			}
			else if (c == '!')
			{
				nextExpressionNegates = true;
			}
			else
			{
				if (!textStarted)
				{
					textStarted = true;
					currentExpression->children.push_back(Expression());
					currentExpression->children.back().negated = nextExpressionNegates;
				}

				if (c == '&' || c == '|')
				{
					if (currentExpression->children.empty() || nextExpressionNegates)
					{
						syntaxError = true;
					}
					else if (!currentExpression->children.back().value.empty() &&
						c != *(currentExpression->children.back().value.end() - 1))
					{
						currentExpression->children.push_back(Expression());
					}
				}

				currentExpression->children.back().value.push_back(c);
				nextExpressionNegates = false;
			}

			++it;
		}

		bool retVal = false;

		if (!expressionParents.empty())
			syntaxError = true;

		if (!syntaxError)
			retVal = evaluateExpressionRecursive(outExpressions, syntaxError, properties);

		if (syntaxError)
			printf("Syntax Error at line %lu\n", calculateLineCount(subString));

		outSyntaxError = syntaxError;

		return retVal;
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::evaluateExpressionRecursive(ExpressionVec &expression, bool &outSyntaxError, PropertyMap &properties)
	{
		ExpressionVec::iterator itor = expression.begin();
		ExpressionVec::iterator end = expression.end();

		while (itor != end)
		{
			if (itor->value == "&&")
				itor->type = EXPR_OPERATOR_AND;
			else if (itor->value == "||")
				itor->type = EXPR_OPERATOR_OR;
			else if (!itor->children.empty())
				itor->type = EXPR_OBJECT;
			else
				itor->type = EXPR_VAR;

			++itor;
		}

		bool syntaxError = outSyntaxError;
		bool lastExpWasOperator = true;

		itor = expression.begin();

		while (itor != end && !syntaxError)
		{
			Expression &exp = *itor;
			if (((exp.type == EXPR_OPERATOR_OR || exp.type == EXPR_OPERATOR_AND) && lastExpWasOperator) ||
				((exp.type == EXPR_VAR || exp.type == EXPR_OBJECT) && !lastExpWasOperator))
			{
				syntaxError = true;
				printf("Unrecognized token '%s'", exp.value.c_str());
			}
			else if (exp.type == EXPR_OPERATOR_OR || exp.type == EXPR_OPERATOR_AND)
			{
				lastExpWasOperator = true;
			}
			else if (exp.type == EXPR_VAR)
			{
				exp.result = properties.getProperty(exp.value) != 0;
				lastExpWasOperator = false;
			}
			else
			{
				exp.result = evaluateExpressionRecursive(exp.children, syntaxError, properties);
				lastExpWasOperator = false;
			}

			++itor;
		}

		bool retVal = true;

		if (!syntaxError)
		{
			itor = expression.begin();
			bool andMode = true;

			while (itor != end)
			{
				if (itor->type == EXPR_OPERATOR_OR)
					andMode = false;
				else if (itor->type == EXPR_OPERATOR_AND)
					andMode = true;
				else
				{
					if (andMode)
						retVal &= itor->negated ? !itor->result : itor->result;
					else
						retVal |= itor->negated ? !itor->result : itor->result;
				}

				++itor;
			}
		}

		outSyntaxError = syntaxError;

		return retVal;
	}
	//-----------------------------------------------------------------------------------
	size_t ShaderGenerator::evaluateExpressionEnd(const SubStringRef &outSubString)
	{
		Ogre::String::const_iterator it = outSubString.begin();
		Ogre::String::const_iterator en = outSubString.end();

		int nesting = 0;

		while (it != en && nesting >= 0)
		{
			if (*it == '(')
				++nesting;
			else if (*it == ')')
				--nesting;
			++it;
		}

		assert(nesting >= -1);

		size_t retVal = Ogre::String::npos;
		if (it != en && nesting < 0)
		{
			retVal = it - outSubString.begin() - 1;
		}
		else
		{
			printf("Syntax Error at line %lu: opening parenthesis without matching closure\n",
				calculateLineCount(outSubString));
		}

		return retVal;
	}
	//-----------------------------------------------------------------------------------
	void ShaderGenerator::evaluateParamArgs(SubStringRef &outSubString, Ogre::StringVector &outArgs,
		bool &outSyntaxError)
	{
		size_t expEnd = evaluateExpressionEnd(outSubString);

		if (expEnd == Ogre::String::npos)
		{
			outSyntaxError = true;
			return;
		}

		SubStringRef subString(&outSubString.getOriginalBuffer(), outSubString.getStart(),
			outSubString.getStart() + expEnd);

		outSubString = SubStringRef(&outSubString.getOriginalBuffer(),
			outSubString.getStart() + expEnd + 1);

		int expressionState = 0;
		bool syntaxError = false;

		outArgs.clear();
		outArgs.push_back(Ogre::String());

		Ogre::String::const_iterator it = subString.begin();
		Ogre::String::const_iterator en = subString.end();

		while (it != en && !syntaxError)
		{
			char c = *it;

			if (c == '(' || c == ')' || c == '@' || c == '&' || c == '|')
			{
				syntaxError = true;
			}
			else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				if (expressionState == 1)
					expressionState = 2;
			}
			else if (c == ',')
			{
				expressionState = 0;
				outArgs.push_back(Ogre::String());
			}
			else
			{
				if (expressionState == 2)
				{
					printf("Syntax Error at line %lu: ',' or ')' expected\n",
						calculateLineCount(subString));
					syntaxError = true;
				}
				else
				{
					outArgs.back().push_back(*it);
					expressionState = 1;
				}
			}

			++it;
		}

		if (syntaxError)
			printf("Syntax Error at line %lu\n", calculateLineCount(subString));

		outSyntaxError = syntaxError;
	}
	//-----------------------------------------------------------------------------------
	void ShaderGenerator::copy(Ogre::String &outBuffer, const SubStringRef &inSubString, size_t length)
	{
		Ogre::String::const_iterator itor = inSubString.begin();
		Ogre::String::const_iterator end = inSubString.begin() + length;

		while (itor != end)
			outBuffer.push_back(*itor++);
	}
	//-----------------------------------------------------------------------------------
	void ShaderGenerator::repeat(Ogre::String &outBuffer, const SubStringRef &inSubString, size_t length,
		size_t passNum, const Ogre::String &counterVar)
	{
		Ogre::String::const_iterator itor = inSubString.begin();
		Ogre::String::const_iterator end = inSubString.begin() + length;

		while (itor != end)
		{
			if (*itor == '@' && !counterVar.empty())
			{
				SubStringRef subString(&inSubString.getOriginalBuffer(), itor + 1);
				if (subString.find(counterVar) == 0)
				{
					char tmp[16];
					sprintf(tmp, "%lu", passNum);
					outBuffer += tmp;
					itor += counterVar.size() + 1;
				}
				else
				{
					outBuffer.push_back(*itor++);
				}
			}
			else
			{
				outBuffer.push_back(*itor++);
			}
		}
	}
	//-----------------------------------------------------------------------------------
	int setOp(int op1, int op2) { return op2; }
	int addOp(int op1, int op2) { return op1 + op2; }
	int subOp(int op1, int op2) { return op1 - op2; }
	int mulOp(int op1, int op2) { return op1 * op2; }
	int divOp(int op1, int op2) { return op1 / op2; }
	int modOp(int op1, int op2) { return op1 % op2; }

	struct Operation
	{
		const char *opName;
		size_t length;
		int(*opFunc)(int, int);
		Operation(const char *_name, size_t len, int(*_opFunc)(int, int)) :
			opName(_name), length(len), opFunc(_opFunc) {}
	};

	const Operation c_operations[6] =
	{
		Operation("pset", sizeof("@pset"), &setOp),
		Operation("padd", sizeof("@padd"), &addOp),
		Operation("psub", sizeof("@psub"), &subOp),
		Operation("pmul", sizeof("@pmul"), &mulOp),
		Operation("pdiv", sizeof("@pdiv"), &divOp),
		Operation("pmod", sizeof("@pmod"), &modOp)
	};
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::parseMath(const Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		Ogre::StringVector argValues;
		SubStringRef subString(&inBuffer, 0);

		size_t pos;
		pos = subString.find("@");
		size_t keyword = ~0;

		while (pos != Ogre::String::npos && keyword == (size_t)~0)
		{
			size_t maxSize = subString.findFirstOf(" \t(", pos + 1);
			maxSize = maxSize == Ogre::String::npos ? subString.getSize() : maxSize;
			SubStringRef keywordStr(&inBuffer, subString.getStart() + pos + 1,
				subString.getStart() + maxSize);

			for (size_t i = 0; i < 6 && keyword == (size_t)~0; ++i)
			{
				if (keywordStr.matchEqual(c_operations[i].opName))
					keyword = i;
			}

			if (keyword == (size_t)~0)
				pos = subString.find("@", pos + 1);
		}

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + c_operations[keyword].length);
			evaluateParamArgs(subString, argValues, syntaxError);

			syntaxError |= argValues.size() < 2 || argValues.size() > 3;

			if (!syntaxError)
			{
				IdString dstProperty;
				IdString srcProperty;
				int op1Value;
				int op2Value;

				dstProperty = argValues[0];
				size_t idx = 1;
				srcProperty = dstProperty;
				if (argValues.size() == 3)
					srcProperty = argValues[idx++];
				op1Value = properties.getProperty(srcProperty);
				op2Value = Ogre::StringConverter::parseInt(argValues[idx],
					-std::numeric_limits<int>::max());

				if (op2Value == -std::numeric_limits<int>::max())
				{
					//Not a number, interpret as property
					op2Value = properties.getProperty(argValues[idx]);
				}

				int result = c_operations[keyword].opFunc(op1Value, op2Value);
				properties.setProperty(dstProperty, result);
			}
			else
			{
				size_t lineCount = calculateLineCount(subString);
				if (keyword <= 1)
				{
					printf("Syntax Error at line %lu: @%s expects one parameter",
						lineCount, c_operations[keyword].opName);
				}
				else
				{
					printf("Syntax Error at line %lu: @%s expects two or three parameters",
						lineCount, c_operations[keyword].opName);
				}
			}

			pos = subString.find("@");
			keyword = ~0;

			while (pos != Ogre::String::npos && keyword == (size_t)~0)
			{
				size_t maxSize = subString.findFirstOf(" \t(", pos + 1);
				maxSize = maxSize == Ogre::String::npos ? subString.getSize() : maxSize;
				SubStringRef keywordStr(&inBuffer, subString.getStart() + pos + 1,
					subString.getStart() + maxSize);

				for (size_t i = 0; i < 6 && keyword == (size_t)~0; ++i)
				{
					if (keywordStr.matchEqual(c_operations[i].opName))
						keyword = i;
				}

				if (keyword == (size_t)~0)
					pos = subString.find("@", pos + 1);
			}
		}

		copy(outBuffer, subString, subString.getSize());

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::parseForEach(const Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		Ogre::StringVector argValues;
		SubStringRef subString(&inBuffer, 0);
		size_t pos = subString.find("@foreach");

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + sizeof("@foreach"));
			evaluateParamArgs(subString, argValues, syntaxError);

			SubStringRef blockSubString = subString;
			findBlockEnd(blockSubString, syntaxError);

			if (!syntaxError)
			{
				char *endPtr;

				// Arg 1 (var)
				Ogre::String counterVar;
				counterVar = argValues[0];

				// Agr 2 (start)
				int start = strtol(argValues[1].c_str(), &endPtr, 10);
				if (argValues[1].c_str() == endPtr)
				{
					//This isn't a number. Let's try if it's a variable
					start = properties.getProperty(argValues[1], 0);
				}

				// Arg 3 (count)
				int count = strtol(argValues[2].c_str(), &endPtr, 10);
				if (argValues[2].c_str() == endPtr)
				{
					//This isn't a number. Let's try if it's a variable
					count = properties.getProperty(argValues[2], 0);
				}

				// Repead the block
				for (int i = start; i < count; ++i)
					repeat(outBuffer, blockSubString, blockSubString.getSize(), i, counterVar);
			}

			subString.setStart(blockSubString.getEnd() + sizeof("@end"));
			pos = subString.find("@foreach");
		}

		copy(outBuffer, subString, subString.getSize());

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::parseProperties(Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		SubStringRef subString(&inBuffer, 0);
		size_t pos = subString.find("@property");

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + sizeof("@property"));
			bool result = evaluateExpression(subString, syntaxError, properties);

			SubStringRef blockSubString = subString;
			findBlockEnd(blockSubString, syntaxError);

			if (result && !syntaxError)
				copy(outBuffer, blockSubString, blockSubString.getSize());

			subString.setStart(blockSubString.getEnd() + sizeof("@end") - 1);
			pos = subString.find("@property");
		}

		copy(outBuffer, subString, subString.getSize());

		while (!syntaxError && outBuffer.find("@property") != Ogre::String::npos)
		{
			inBuffer.swap(outBuffer);
			syntaxError = parseProperties(inBuffer, outBuffer, properties);
		}

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::collectPieces(const Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties, PiecesMap& pieces)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		Ogre::StringVector argValues;
		SubStringRef subString(&inBuffer, 0);
		size_t pos = subString.find("@piece");

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + sizeof("@piece"));
			evaluateParamArgs(subString, argValues, syntaxError);

			syntaxError |= argValues.size() != 1;

			if (!syntaxError)
			{
				const IdString pieceName(argValues[0]);
				PiecesMap::const_iterator it = pieces.find(pieceName);
				if (it != pieces.end())
				{
					syntaxError = true;
					printf("Error at line %lu: @piece '%s' already defined",
						calculateLineCount(subString), argValues[0].c_str());
				}
				else
				{
					SubStringRef blockSubString = subString;
					findBlockEnd(blockSubString, syntaxError);

					Ogre::String tmpBuffer;
					copy(tmpBuffer, blockSubString, blockSubString.getSize());
					pieces[pieceName] = tmpBuffer;

					subString.setStart(blockSubString.getEnd() + sizeof("@end"));
				}
			}
			else
			{
				printf("Syntax Error at line %lu: @piece expects one parameter",
					calculateLineCount(subString));
			}

			pos = subString.find("@piece");
		}

		copy(outBuffer, subString, subString.getSize());

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::insertPieces(Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties, PiecesMap& pieces)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		Ogre::StringVector argValues;
		SubStringRef subString(&inBuffer, 0);
		size_t pos = subString.find("@insertpiece");

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + sizeof("@insertpiece"));
			evaluateParamArgs(subString, argValues, syntaxError);

			syntaxError |= argValues.size() != 1;

			if (!syntaxError)
			{
				const IdString pieceName(argValues[0]);
				PiecesMap::const_iterator it = pieces.find(pieceName);
				if (it != pieces.end())
					outBuffer += it->second;
			}
			else
			{
				printf("Syntax Error at line %lu: @insertpiece expects one parameter",
					calculateLineCount(subString));
			}

			pos = subString.find("@insertpiece");
		}

		copy(outBuffer, subString, subString.getSize());

		while (!syntaxError && outBuffer.find("@insertpiece") != Ogre::String::npos)
		{
			inBuffer.swap(outBuffer);
			syntaxError = insertPieces(inBuffer, outBuffer, properties, pieces);
		}

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	const Operation c_counterOperations[8] =
	{
		Operation("counter", sizeof("@counter"), 0),
		Operation("value", sizeof("@value"), 0),
		Operation("set", sizeof("@set"), &setOp),
		Operation("add", sizeof("@add"), &addOp),
		Operation("sub", sizeof("@sub"), &subOp),
		Operation("mul", sizeof("@mul"), &mulOp),
		Operation("div", sizeof("@div"), &divOp),
		Operation("mod", sizeof("@mod"), &modOp)
	};
	//-----------------------------------------------------------------------------------
	bool ShaderGenerator::parseCounter(const Ogre::String &inBuffer, Ogre::String &outBuffer, PropertyMap &properties)
	{
		outBuffer.clear();
		outBuffer.reserve(inBuffer.size());

		Ogre::StringVector argValues;
		SubStringRef subString(&inBuffer, 0);

		size_t pos;
		pos = subString.find("@");
		size_t keyword = ~0;

		if (pos != Ogre::String::npos)
		{
			size_t maxSize = subString.findFirstOf(" \t(", pos + 1);
			SubStringRef keywordStr(&inBuffer, subString.getStart() + pos + 1,
				subString.getStart() + maxSize);

			for (size_t i = 0; i < 8 && keyword == (size_t)~0; ++i)
			{
				if (keywordStr.matchEqual(c_counterOperations[i].opName))
					keyword = i;
			}

			if (keyword == (size_t)~0)
				pos = Ogre::String::npos;
		}

		bool syntaxError = false;

		while (pos != Ogre::String::npos && !syntaxError)
		{
			//Copy what comes before the block
			copy(outBuffer, subString, pos);

			subString.setStart(subString.getStart() + pos + c_counterOperations[keyword].length);
			evaluateParamArgs(subString, argValues, syntaxError);

			if (keyword <= 1)
				syntaxError |= argValues.size() != 1;
			else
				syntaxError |= argValues.size() < 2 || argValues.size() > 3;

			if (!syntaxError)
			{
				IdString dstProperty;
				IdString srcProperty;
				int op1Value;
				int op2Value;

				if (argValues.size() == 1)
				{
					dstProperty = argValues[0];
					srcProperty = dstProperty;
					op1Value = properties.getProperty(srcProperty);
					op2Value = op1Value;

					//@value & @counter write, the others are invisible
					char tmp[16];
					sprintf(tmp, "%i", op1Value);
					outBuffer += tmp;

					if (keyword == 0)
					{
						++op1Value;
						properties.setProperty(dstProperty, op1Value);
					}
				}
				else
				{
					dstProperty = argValues[0];
					size_t idx = 1;
					srcProperty = dstProperty;
					if (argValues.size() == 3)
						srcProperty = argValues[idx++];
					op1Value = properties.getProperty(srcProperty);
					op2Value = Ogre::StringConverter::parseInt(argValues[idx],
						-std::numeric_limits<int>::max());

					if (op2Value == -std::numeric_limits<int>::max())
					{
						//Not a number, interpret as property
						op2Value = properties.getProperty(argValues[idx]);
					}

					int result = c_counterOperations[keyword].opFunc(op1Value, op2Value);
					properties.setProperty(dstProperty, result);
				}
			}
			else
			{
				size_t lineCount = calculateLineCount(subString);
				if (keyword <= 1)
				{
					printf("Syntax Error at line %lu: @%s expects one parameter",
						lineCount, c_counterOperations[keyword].opName);
				}
				else
				{
					printf("Syntax Error at line %lu: @%s expects two or three parameters",
						lineCount, c_counterOperations[keyword].opName);
				}
			}

			pos = subString.find("@");
			keyword = ~0;

			if (pos != Ogre::String::npos)
			{
				size_t maxSize = subString.findFirstOf(" \t(", pos + 1);
				SubStringRef keywordStr(&inBuffer, subString.getStart() + pos + 1,
					subString.getStart() + maxSize);

				for (size_t i = 0; i < 8 && keyword == (size_t)~0; ++i)
				{
					if (keywordStr.matchEqual(c_counterOperations[i].opName))
						keyword = i;
				}

				if (keyword == (size_t)~0)
					pos = Ogre::String::npos;
			}
		}

		copy(outBuffer, subString, subString.getSize());

		return syntaxError;
	}
	//-----------------------------------------------------------------------------------
	Ogre::String ShaderGenerator::parse(Ogre::String &inBuffer, PropertyMap &properties, Ogre::StringVector& pieceFiles)
	{
		Ogre::String outBuffer;
		outBuffer.reserve(inBuffer.size());

		//Collect pieces
		auto itor = pieceFiles.begin();
		auto end = pieceFiles.end();

		Ogre::String inPiece;
		Ogre::String outPiece;

		PiecesMap pieces;

		while (itor != end)
		{
			parseMath(*itor, inPiece, properties);
			parseForEach(inPiece, outPiece, properties);
			parseProperties(outPiece, inPiece, properties);
			collectPieces(inPiece, outPiece, properties, pieces);
			++itor;
		}

		parseMath(inBuffer, outBuffer, properties);
		parseForEach(outBuffer, inBuffer, properties);
		parseProperties(inBuffer, outBuffer, properties);
		collectPieces(outBuffer, inBuffer, properties, pieces);
		insertPieces(inBuffer, outBuffer, properties, pieces);
		parseCounter(outBuffer, inBuffer, properties);

		outBuffer.swap(inBuffer);

		return outBuffer;
	}
	//-----------------------------------------------------------------------------------
	size_t ShaderGenerator::calculateLineCount(const Ogre::String &buffer, size_t idx)
	{
		Ogre::String::const_iterator itor = buffer.begin();
		Ogre::String::const_iterator end = buffer.begin() + idx;

		size_t lineCount = 0;

		while (itor != end)
		{
			if (*itor == '\n')
				++lineCount;
			++itor;
		}

		return lineCount + 1;
	}
	//-----------------------------------------------------------------------------------
	size_t ShaderGenerator::calculateLineCount(const SubStringRef &subString)
	{
		return calculateLineCount(subString.getOriginalBuffer(), subString.getStart());
	}
	//-----------------------------------------------------------------------------------
}
