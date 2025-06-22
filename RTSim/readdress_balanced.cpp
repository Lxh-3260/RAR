#include <iostream>
#include <sstream>
#include <cmath>
#include <stdlib.h>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "src/Config.h"
#include "src/Params.h"
#include "include/NVMHelpers.h"
#include "traceReader/TraceReaderFactory.h"
#include "src/AddressTranslator.h"
#include "Decoders/DecoderFactory.h"

using namespace NVM;

std::unordered_map<uint64_t, int> addressVar; // 访问顶点
std::vector<uint64_t> accessSequence;		  // 访问序列
std::vector<std::vector<uint64_t>> banklists; // bank中的数据
std::string addressMappingScheme = "";

std::vector<uint64_t> AIS_addr;

int factor = 5;
int ranks, banks;
int banknum;
Config *config = new Config();
GenericTraceReader *trace = NULL;
TraceLine *tl = new TraceLine();
AddressTranslator *translator;
TranslationMethod *method;

void DataPlacement();
void RewriteAddress();

int main(int argc, char *argv[])
{
	if (argc < 3) // 参数个数小于3会停止运行
	{
		std::cout << "Usage: ReAddress CONFIG_FILE TRACE_FILE" // 命令行结构：“ReAddress程序 config文件 trace文件”
				  << std::endl;
		return 1;
	}

	/* Print out the command line that was provided. */
	std::cout << std::endl
			  << "ReAddress command line is:" << std::endl;
	for (int curArg = 0; curArg < argc; ++curArg)
	{
		std::cout << argv[curArg] << " ";
	}
	std::cout << std::endl
			  << std::endl;

	config->Read(argv[1]); // 获取config文件的参数

	Params *p = new Params();
	p->SetParams(config); // 复制conf中的相关信息到params中；如果指定了“MemType”，p->DBCS = p->ROWS = "DBCS"对应的值，p->COLS = p->DOMAINS = "DOMAINS"对应的值

	int channels, rows, cols, subarrays; // ranks, banks,
	if (config->KeyExists("MATHeight"))
	{
		rows = static_cast<int>(p->MATHeight);
		subarrays = static_cast<int>(p->ROWS / p->MATHeight);
	}
	else
	{
		rows = static_cast<int>(p->ROWS);
		subarrays = 1;
	}
	cols = static_cast<int>(p->COLS);
	banks = static_cast<int>(p->BANKS);
	ranks = static_cast<int>(p->RANKS);
	channels = static_cast<int>(p->CHANNELS);

	method = new TranslationMethod();
	method->SetBitWidths(NVM::mlog2(rows),
						 NVM::mlog2(cols),
						 NVM::mlog2(banks),
						 NVM::mlog2(ranks),
						 NVM::mlog2(channels),
						 NVM::mlog2(subarrays));
	method->SetCount(rows, cols, banks, ranks, channels, subarrays);
	method->SetAddressMappingScheme(p->AddressMappingScheme); // RM.config设置的：p->AddressMappingScheme = "RK:BK:CH:R:C"     默认情况："R:SA:RK:BK:CH:C"

	addressMappingScheme = config->GetString("AddressMappingScheme");
	if (config->KeyExists("TraceReader"))
		trace = TraceReaderFactory::CreateNewTraceReader(
			config->GetString("TraceReader"));
	else
		trace = TraceReaderFactory::CreateNewTraceReader("NVMainTrace");

	if (config->KeyExists("Decoder"))
		translator = DecoderFactory::CreateNewDecoder(config->GetString("Decoder"));
	else
		translator = new AddressTranslator();
	translator->SetTranslationMethod(method);

	banknum = ranks * banks; // bank的总个数
	for (int i = 0; i < banknum; i++)
	{
		std::vector<uint64_t> bank;
		banklists.push_back(bank);
	}

	trace->SetTraceFile(argv[2]);
	DataPlacement();
	RewriteAddress();
	// AIS_Dataplacement();
	// AIS_rewriteAddress();
	return 0;
}

bool cmp_weight(std::pair<int, int> lhs, std::pair<int, int> rhs)
{
	return lhs.second > rhs.second;
}

void DataPlacement()
{ // trace文件最后一行必须有一行空行
	bool finish = false;
	while (!finish)
	{
		/* 以长度为 “factor*BANKS个数” 的访问序列为一批，分批处理 */
		addressVar.clear();
		accessSequence.clear();
		int count = 0;
		int varnum = 0;
		while (count < factor * banknum)
		{
			if (!trace->GetNextAccess(tl)) // tl = trace->nextaccess
			{
				std::cout << "Could not read next line from trace file!"
						  << std::endl;
				finish = true;
				break;
			}
			uint64_t address = tl->GetAddress().GetPhysicalAddress();
			std::cout << count << ": 0x" << std::hex << address << std::dec << std::endl;
			if (addressVar.find(address) == addressVar.end())
				addressVar.insert(std::pair<uint64_t, int>(address, varnum++));
			accessSequence.push_back(address);
			count++;
		}

		/* 确定这部分trace的数据放置策略 */
		std::vector<std::vector<int>> accessGraph(varnum, std::vector<int>(varnum, 0)); // 访问图，下标为addressVar中address对应的值
		for (int i = 0; i < count - 1; i++)
		{										   // 遍历访问序列，构建访问图
			int x = addressVar[accessSequence[i]]; // accessSequence[i]在访问图中对应的下标x
			int y = addressVar[accessSequence[i + 1]];
			accessGraph[x][y]++;
			accessGraph[y][x]++;
		}

		std::vector<std::pair<int, int>> varWeights; //<序号, 顶点权重>
		for (int i = 0; i < varnum; i++)			 // 初始化
			varWeights.push_back(std::pair<int, int>(i, 0));
		for (int i = 0; i < varnum; i++)
		{ // 对每个顶点求顶点权重
			for (int j = 0; j < varnum; j++)
			{
				varWeights[i].second += accessGraph[i][j];
			}
		}

		std::sort(varWeights.begin(), varWeights.end(), cmp_weight); // 对varWeights按第二个属性从大到小排序

		for (auto it = varWeights.begin(); it != varWeights.end(); it++) // 依次对该组地址确定放置位置
		{
			int index = it->first; // 当前顶点权重最大的顶点的序号
			uint64_t address;
			for (auto v = addressVar.begin(); v != addressVar.end(); v++) // 找到对应的地址号
			{
				if (index == v->second)
				{
					address = v->first;
					break;
				}
			}
			/* 判断是否已经在Bank中 */
			bool isExist = false;
			for (auto bank = banklists.begin(); bank != banklists.end(); bank++)
			{
				if (std::find(bank->begin(), bank->end(), address) != bank->end()) // 能在某个bank中找到
				{
					isExist = true;
					break;
				}
			}
			if (isExist)
				continue;
			/* 不在Bank中的后续操作 */
			std::vector<int> bankWeight; // bankWeight[i]表示第i个bank与当前address的关联度
			for (int i = 0; i < banknum; i++)
			{
				int weight = 0;
				for (unsigned int j = 0; j < banklists[i].size(); j++) // 遍历第i个bank，计算该bank与当前address的关联度
				{
					if (addressVar.find(banklists[i][j]) != addressVar.end()) // 第i个bank中的第j个数据属于该轮访问序列
					{
						int x = addressVar[address];
						int y = addressVar[banklists[i][j]];
						weight += accessGraph[x][y];
					}
				}
				bankWeight.push_back(weight);
			}

			int minbank = 0;
			for (unsigned int i = 1; i < bankWeight.size(); i++) // 令minbank指向与当前address关联度最小的bank的序号（若有多个关联度最小的bank，选其中已存放数据量最少的bank）
			{
				// if(bankWeight[minbank] > bankWeight[i] ){   //无负载均衡
				//     minbank = i;
				// }
				if (bankWeight[minbank] > bankWeight[i] || (bankWeight[minbank] == bankWeight[i] && banklists[minbank].size() > banklists[i].size()))
				{ // 负载均衡
					minbank = i;
				}
			}

			banklists[minbank].push_back(address); // 将当前trace的地址加入第minbank个Bank中
		}
	}
	for (int i = 0; i < banknum; i++)
	{
		std::cout << "Bank " << i << ":" << std::endl;
		for (unsigned int j = 0; j < banklists[i].size(); j++)
		{
			std::cout << "0x" << std::hex << banklists[i][j] << std::dec << std::endl;
		}
	}
	std::cout << "Data placement finished!" << std::endl;
	std::cout << "Total number of banks: " << banknum << std::endl;
	for (int i = 0; i < banknum; i++)
	{
		std::cout << "Bank " << i << " has " << banklists[i].size() << " objs." << std::endl;
	}
}

void RewriteAddress()
{
	std::string tracefile = trace->GetTraceFile();
	std::ifstream inFile(tracefile.c_str());
	// std::cout<< tracefile << std::endl;
	if (!inFile)
	{
		std::cerr << "Unable to open input file: " << tracefile << std::endl;
	}

	std::string insertStr = "_ra_balanced"; // 要插入的字符串
	std::string suffix = ".nvt";			// 后缀
	std::string retracefile = tracefile;
	size_t pos = retracefile.find(suffix);
	if (pos != std::string::npos)
	{
		retracefile.insert(pos, insertStr);
	}
	// std::cout<< retracefile << std::endl;
	std::ofstream outFile(retracefile.c_str());
	if (!outFile)
	{
		std::cerr << "Unable to open output file: " << retracefile << std::endl;
	}

	if (inFile.is_open())
	{
		std::string line;
		while (std::getline(inFile, line))
		{ // 逐行读取文件内容，存到line中
			if (line.substr(0, 4) == "NVMV")
			{
				outFile << line << std::endl;
				std::getline(inFile, line);
			}
			std::istringstream lineStream(line);
			std::string field; // fullLine中的一个切片（以' '为分隔符）
			unsigned char fieldId = 0;

			/*
			 *  Again, the format is : CYCLE OP ADDRESS DATA THREADID
			 *  So the field ids are :   0    1    2      3      4
			 */
			while (getline(lineStream, field, ' '))
			{
				if (field != "") // 如果切片不为空
				{
					if (fieldId == 2) // 是第3个切片
					{
						uint64_t address;
						std::stringstream fmat;
						fmat << std::hex << field;
						fmat >> address;

						int bankId = -1;
						int rankId = -1;
						for (int i = 0; i < banknum; i++)
						{
							if (std::find(banklists[i].begin(), banklists[i].end(), address) != banklists[i].end()) // 能在某个bank中找到
							{
								rankId = i / banks;
								bankId = i % banks;
								break;
							}
						}

						uint64_t channel, rank, bank, row, col, subarray;
						translator->Translate(address, &row, &col, &rank, &bank, &channel, &subarray);

						u_int64_t readdress = translator->ReWriteAddress(address, rankId, bankId);
						uint64_t rechannel, rerank, rebank, rerow, recol, resubarray;
						translator->Translate(readdress, &rerow, &recol, &rebank, &rerank, &rechannel, &resubarray);
						outFile << "0x" << std::hex << readdress << " ";
					}
					else if (fieldId == 5)
					{
						outFile << field << std::endl;
					}
					else
					{
						outFile << field << " ";
					}

					fieldId++;
				}
			}
		}
	}
	inFile.close();
	outFile.close();
}