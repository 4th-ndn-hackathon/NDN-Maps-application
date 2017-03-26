import javax.imageio.*;
import java.io.*;
import java.net.*;

public class ImgCon {

	public static void main(String[] args) throws IOException
	{

		URL url = new URL("http://tile.openstreetmap.org/7/63/43.png");
		InputStream in = new BufferedInputStream(url.openStream());
		ByteArrayOutputStream out = new ByteArrayOutputStream();
		byte[] buf = new byte[1024];
		int n = 0;
		while (-1!=(n=in.read(buf)))
		{
		   out.write(buf, 0, n);
		}
		out.close();
		in.close();
		byte[] response = out.toByteArray();
		System.out.println(stringThis(response));
    }
	
	public static String stringThis(byte[] r)
	{
		String s = "";
		for(int i = 0; i < r.length; i++)
		{
			s += " " + r[i] + " ";
		}
		return s;
	}
}