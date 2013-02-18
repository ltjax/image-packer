
#define REPLAY_USE_LIBPNG

#include <replay/pixbuf.hpp>
#include <replay/pixbuf_io.hpp>
#include <replay/box_packer.hpp>
#include <replay/vector2.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/fstream.hpp>
#include <replay/pixbuf_io.hpp>

struct ImageEntryType
{
	ImageEntryType() : IsNinePatch(false) {}
	
	boost::filesystem::path	RelativePath;
	replay::shared_pixbuf	Image;
	replay::box<int>		Box;
	
	bool					IsNinePatch;
	
	replay::vector2<int>	XCenter;
	replay::vector2<int>	XContents;
	
	replay::vector2<int>	YCenter;
	replay::vector2<int>	YContents;
};

void ScanFile(std::vector<ImageEntryType>& List, boost::filesystem::path const& Path)
{
	auto AddFile=[&](boost::filesystem::path const& File, boost::filesystem::path const& Relative)
	{
		if (extension(File)!=".png")
			return;
		
		std::cout << "Loading " << File.string() << std::endl;
		
		auto Image=replay::pixbuf_io::load_from_file(File);
		
		ImageEntryType Entry;
		Entry.Image=Image;
		Entry.RelativePath=Relative;
		
		List.push_back(Entry);
		
	};
	
	if (!is_directory(Path))
	{
		AddFile(Path, Path.leaf());
		return;
	}
	
	using namespace boost::filesystem;
	typedef boost::filesystem::recursive_directory_iterator Iterator;
	std::size_t BaseOffset=std::distance(Path.begin(), Path.end());
	for (Iterator i(Path), ie; i!=ie; ++i)
	{
		if (is_symlink(*i))
			i.no_push();
		
		if (!is_regular_file(*i))
			continue;
		
		path Absolute=*i;
		path Relative;
		auto j=Absolute.begin();
		
		std::size_t c=0;
		for (; j!=Absolute.end(); ++j, ++c)
			if (c >= BaseOffset)
				Relative /= *j;
		
		if (!Relative.empty())
			AddFile(Path/Relative, Relative);
	}
}

bool PackInto(std::vector<ImageEntryType>& List, int Width, int Height)
{
	replay::box_packer Packer(Width, Height);
	
	for (auto i=begin(List); i!=end(List); ++i)
		if (!Packer.pack(i->Image->get_width(), i->Image->get_height(), &i->Box))
			return false;
	
	return true;
}

void BlitImages(replay::pixbuf& Result, std::vector<ImageEntryType> const& List)
{
	for (auto i=List.begin(); i!=List.end(); ++i)
	{
		i->Image->convert_to_rgba();
		
		if (!Result.blit(i->Box.left, i->Box.bottom, *i->Image))
			throw std::runtime_error("Unable to blit image" + i->RelativePath.string());
	}
}

void PackImages(boost::filesystem::path const& ResultImage, std::vector<ImageEntryType>& List)
{
	// Start by trying to open the result file
	boost::filesystem::ofstream File(ResultImage, std::ios::binary|std::ios::trunc);
	
	if (!File.good())
		throw std::runtime_error("Unable to open target file: "+ResultImage.string());
	
	// Use an appropriate starting size
	int PixelCount=0;
	for (auto i=List.begin(); i!=List.end(); ++i)
	{
		auto&& Image(*i->Image);
		PixelCount+=Image.get_width()*Image.get_height();
	}
	
	int Width=128, Height=128;
	
	while (Width*Height < PixelCount || !PackInto(List, Width, Height))
	{
		if (Width <= Height)
			Width *= 2;
		else
			Height *= 2;
	}
	
	auto Result=replay::pixbuf::create(Width, Height, replay::pixbuf::rgba);
	
	BlitImages(*Result, List);
	
	replay::pixbuf_io::save_to_png_file(File, *Result, 9);
}

void WriteDictionary(std::ofstream& File, const std::string& DictionaryName, std::vector<ImageEntryType> const& List)
{
	File << "local " << DictionaryName << "= {\n";
	
	for (auto i=List.begin(); i!=List.end(); ++i)
	{
		auto&& Box=i->Box;
		File << "  \"" << i->RelativePath.string() << "\"={Box={" << Box.left << "," << Box.bottom << "," << Box.right << "," << Box.top << "}}";
		
		if (i+1 != List.end())
			File << ",\n";
		else
			File << "\n";
	}
	
	File << "}\n";
}

void MakePackedImage(
	boost::filesystem::path const& ImagePath,
	boost::filesystem::path const& DictionaryPath,
	std::string const& DictionaryName,
	std::vector<std::string> const& SourceList)
{
	
	std::vector<ImageEntryType> ImageList;
	
	boost::filesystem::ofstream DictionaryFile(DictionaryPath, std::ios::trunc);
	
	for (auto i=begin(SourceList); i!=end(SourceList); ++i)
	{
		ScanFile(ImageList, *i);
	}
	
	PackImages(ImagePath, ImageList);
	WriteDictionary(DictionaryFile, DictionaryName, ImageList);
}

int main(int argc, char* argv[])
{
	namespace po = boost::program_options;
	
	namespace po = boost::program_options;
	po::options_description desc("Allowed options");
	
	
	std::string ImagePath, DictionaryPath, DictionaryName;
	std::string Code;
	std::vector<std::string> SourceList;
	
	desc.add_options()
	("image-path", po::value<std::string>(&ImagePath)->default_value("packed_image.png"), "Set the path where to write the packed image data")
	("dict-path", po::value<std::string>(&DictionaryPath)->default_value("packed_image.lua"), "Set the path where to write the dictionary for the image data")
	("dict-name", po::value<std::string>(&DictionaryName)->default_value("Dictionary"), "Set the variable-name for the generated table")
	("image", po::value<std::vector<std::string>>(&SourceList), "Individual paths or folders to use as sources")
	;
	
	po::positional_options_description p;
	p.add("image", -1);
	
	po::variables_map VariableMap;
	po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), VariableMap);
	
	po::notify(VariableMap);
	
	try {
		MakePackedImage(ImagePath, DictionaryPath, DictionaryName, SourceList);
		
	} catch (std::exception const& Error)
	{
		std::cerr << Error.what();
	}
	
	return 0;
}
