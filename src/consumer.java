
import java.io.IOException;

import net.named_data.jndn.Data;
import net.named_data.jndn.Face;
import net.named_data.jndn.Interest;
import net.named_data.jndn.Name;
import net.named_data.jndn.OnData;
import net.named_data.jndn.OnTimeout;

public class consumer implements OnTimeout, OnData {

	public static void main(String[] args) {

		System.out.println("starting Java Consumer");
		consumer consumer = new consumer();
		Face face = new Face();
		// String s= "/ww/ww";
		Interest interest = new Interest(new Name("/maps/7/63/43"));
		System.out.println(interest.getName().toString());
		interest.setMustBeFresh(true);
		interest.setInterestLifetimeMilliseconds(2000);

		try {
			face.expressInterest(interest, consumer, consumer);
			while (true) {
				Thread.sleep(10);
				face.processEvents();
			}
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
	}

	@SuppressWarnings("deprecation")
	@Override
	public void onData(Interest arg0, Data arg1) {
		System.out.println("On Data ");
		System.out.println(String.valueOf(arg1.getContent().toString()));

	}

	@Override
	public void onTimeout(Interest arg0) {
		System.out.println("On TimeOut ");

	}
}